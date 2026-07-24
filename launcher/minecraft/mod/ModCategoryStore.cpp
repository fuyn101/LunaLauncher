// SPDX-License-Identifier: GPL-3.0-only

#include "ModCategoryStore.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>

#include "FileSystem.h"
#include "Json.h"
#include "minecraft/mod/Mod.h"
#include "modplatform/ModIndex.h"

namespace {
constexpr int FORMAT_VERSION = 1;
}

ModCategoryStore::ModCategoryStore(QString instanceRoot, QString scope)
    : m_instanceRoot(std::move(instanceRoot)), m_scope(std::move(scope))
{
    m_scope.replace('\\', '/');
}

QString ModCategoryStore::relativePath()
{
    return QStringLiteral(".lunalauncher/mod-categories.json");
}

QString ModCategoryStore::normalizeAlias(const QString& alias)
{
    const auto normalized = alias.trimmed();
    return normalized.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)
               ? QStringLiteral("file:") + normalized.mid(QStringLiteral("file:").size())
               : normalized.toCaseFolded();
}

QStringList ModCategoryStore::normalizeAliases(const QStringList& aliases)
{
    QStringList normalized;
    QSet<QString> seen;
    for (const auto& alias : aliases) {
        const auto value = normalizeAlias(alias);
        if (!value.isEmpty() && !seen.contains(value)) {
            normalized.append(value);
            seen.insert(value);
        }
    }
    return normalized;
}

QStringList ModCategoryStore::aliasesFor(const Mod& mod)
{
    QStringList aliases;
    const auto fileName = mod.getOriginalFileName().trimmed();
    if (!fileName.isEmpty()) {
        aliases.append(QStringLiteral("file:%1").arg(fileName));
    }

    if (const auto metadata = mod.metadata(); metadata && metadata->project_id.isValid() && !metadata->project_id.isNull()) {
        const auto projectId = metadata->project_id.toString().trimmed();
        if (!projectId.isEmpty()) {
            aliases.append(QStringLiteral("%1:%2").arg(ModPlatform::ProviderCapabilities::name(metadata->provider), projectId));
        }
    }

    const auto modId = mod.mod_id().trimmed();
    if (!modId.isEmpty()) {
        aliases.append(QStringLiteral("modid:%1").arg(modId));
    }
    return normalizeAliases(aliases);
}

bool ModCategoryStore::load()
{
    m_categories.clear();
    m_assignments.clear();
    m_writable = true;

    const auto filePath = QDir(m_instanceRoot).absoluteFilePath(relativePath());
    if (!QFileInfo::exists(filePath)) {
        return true;
    }

    QByteArray data;
    try {
        data = FS::read(filePath);
    } catch (const FS::FileSystemException& e) {
        qWarning() << "Failed to read mod categories:" << e.cause();
        m_writable = false;
        return false;
    }

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "Failed to parse mod categories:" << error.errorString();
        m_writable = false;
        return false;
    }

    const auto root = document.object();
    if (root.value(QStringLiteral("formatVersion")).toInt() != FORMAT_VERSION) {
        qWarning() << "Unsupported mod category format version";
        m_writable = false;
        return false;
    }

    const auto scopesValue = root.value(QStringLiteral("scopes"));
    if (!scopesValue.isObject()) {
        qWarning() << "Invalid mod category file: 'scopes' must be an object";
        m_writable = false;
        return false;
    }
    const auto scopes = scopesValue.toObject();
    if (!scopes.contains(m_scope)) {
        return true;
    }
    const auto scopeValue = scopes.value(m_scope);
    if (!scopeValue.isObject()) {
        qWarning() << "Invalid mod category scope:" << m_scope;
        m_writable = false;
        return false;
    }
    const auto scopeObject = scopeValue.toObject();
    if (!scopeObject.value(QStringLiteral("categories")).isArray() ||
        !scopeObject.value(QStringLiteral("assignments")).isArray()) {
        qWarning() << "Invalid mod category scope arrays:" << m_scope;
        m_writable = false;
        return false;
    }

    QSet<QString> categoryIds;
    QSet<QString> categoryNames;
    for (const auto value : scopeObject.value(QStringLiteral("categories")).toArray()) {
        if (!value.isObject()) {
            m_writable = false;
            return false;
        }
        const auto object = value.toObject();
        if (!object.value(QStringLiteral("id")).isString() || !object.value(QStringLiteral("name")).isString() ||
            (object.contains(QStringLiteral("collapsed")) && !object.value(QStringLiteral("collapsed")).isBool())) {
            m_writable = false;
            return false;
        }
        Category category{ object.value(QStringLiteral("id")).toString().trimmed(),
                           object.value(QStringLiteral("name")).toString().trimmed(),
                           object.value(QStringLiteral("collapsed")).toBool(false) };
        const auto foldedName = category.name.toCaseFolded();
        if (category.id.isEmpty() || category.name.isEmpty() || categoryIds.contains(category.id) || categoryNames.contains(foldedName)) {
            m_writable = false;
            return false;
        }
        categoryIds.insert(category.id);
        categoryNames.insert(foldedName);
        m_categories.append(category);
    }

    QSet<QString> assignedAliases;
    for (const auto value : scopeObject.value(QStringLiteral("assignments")).toArray()) {
        if (!value.isObject()) {
            m_writable = false;
            return false;
        }
        const auto object = value.toObject();
        if (!object.value(QStringLiteral("category")).isString() || !object.value(QStringLiteral("aliases")).isArray()) {
            m_writable = false;
            return false;
        }
        Assignment assignment;
        assignment.categoryId = object.value(QStringLiteral("category")).toString();
        for (const auto alias : object.value(QStringLiteral("aliases")).toArray()) {
            if (!alias.isString()) {
                m_writable = false;
                return false;
            }
            assignment.aliases.append(alias.toString());
        }
        assignment.aliases = normalizeAliases(assignment.aliases);
        if (!containsCategory(assignment.categoryId) || assignment.aliases.isEmpty()) {
            m_writable = false;
            return false;
        }
        for (const auto& alias : assignment.aliases) {
            if (assignedAliases.contains(alias)) {
                m_writable = false;
                return false;
            }
            assignedAliases.insert(alias);
        }
        m_assignments.append(assignment);
    }
    return true;
}

bool ModCategoryStore::save() const
{
    if (!m_writable) {
        return false;
    }
    QJsonObject root;
    const auto filePath = QDir(m_instanceRoot).absoluteFilePath(relativePath());
    if (QFileInfo::exists(filePath)) {
        try {
            QJsonParseError error;
            const auto existing = QJsonDocument::fromJson(FS::read(filePath), &error);
            if (error.error != QJsonParseError::NoError || !existing.isObject() ||
                existing.object().value(QStringLiteral("formatVersion")).toInt() != FORMAT_VERSION) {
                return false;
            }
            root = existing.object();
        } catch (const FS::FileSystemException&) {
            return false;
        }
    }

    root.insert(QStringLiteral("formatVersion"), FORMAT_VERSION);
    auto scopes = root.value(QStringLiteral("scopes")).toObject();
    QJsonObject scopeObject;
    QJsonArray categories;
    for (const auto& category : m_categories) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), category.id);
        object.insert(QStringLiteral("name"), category.name);
        object.insert(QStringLiteral("collapsed"), category.collapsed);
        categories.append(object);
    }
    scopeObject.insert(QStringLiteral("categories"), categories);

    QJsonArray assignments;
    for (const auto& assignment : m_assignments) {
        QJsonObject object;
        object.insert(QStringLiteral("category"), assignment.categoryId);
        QJsonArray aliases;
        for (const auto& alias : assignment.aliases) {
            aliases.append(alias);
        }
        object.insert(QStringLiteral("aliases"), aliases);
        assignments.append(object);
    }
    scopeObject.insert(QStringLiteral("assignments"), assignments);
    scopes.insert(m_scope, scopeObject);
    root.insert(QStringLiteral("scopes"), scopes);

    if (!FS::ensureFilePathExists(filePath)) {
        qWarning() << "Failed to create mod category folder for" << filePath;
        return false;
    }
    try {
        Json::write(root, filePath);
        return true;
    } catch (const FS::FileSystemException& e) {
        qWarning() << "Failed to write mod categories:" << e.cause();
        return false;
    }
}

int ModCategoryStore::categoryIndex(const QString& id) const
{
    for (int i = 0; i < m_categories.size(); ++i) {
        if (m_categories.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

bool ModCategoryStore::containsCategory(const QString& id) const
{
    return categoryIndex(id) >= 0;
}

QString ModCategoryStore::categoryName(const QString& id) const
{
    const auto index = categoryIndex(id);
    return index >= 0 ? m_categories.at(index).name : QString{};
}

bool ModCategoryStore::nameAvailable(const QString& name, const QString& exceptId) const
{
    const auto normalized = name.trimmed();
    if (normalized.isEmpty()) {
        return false;
    }
    for (const auto& category : m_categories) {
        if (category.id != exceptId && category.name.compare(normalized, Qt::CaseInsensitive) == 0) {
            return false;
        }
    }
    return true;
}

QString ModCategoryStore::addCategory(const QString& name)
{
    const auto normalized = name.trimmed();
    if (!m_writable || !nameAvailable(normalized)) {
        return {};
    }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_categories.append({ id, normalized, false });
    return id;
}

bool ModCategoryStore::renameCategory(const QString& id, const QString& name)
{
    const auto index = categoryIndex(id);
    const auto normalized = name.trimmed();
    if (!m_writable || index < 0 || !nameAvailable(normalized, id)) {
        return false;
    }
    m_categories[index].name = normalized;
    return true;
}

bool ModCategoryStore::removeCategory(const QString& id)
{
    const auto index = categoryIndex(id);
    if (!m_writable || index < 0) {
        return false;
    }
    m_categories.removeAt(index);
    m_assignments.removeIf([&id](const Assignment& assignment) { return assignment.categoryId == id; });
    return true;
}

bool ModCategoryStore::moveCategory(const QString& id, int offset)
{
    const auto index = categoryIndex(id);
    const auto destination = index + offset;
    if (!m_writable || index < 0 || destination < 0 || destination >= m_categories.size()) {
        return false;
    }
    m_categories.move(index, destination);
    return true;
}

bool ModCategoryStore::setCollapsed(const QString& id, bool collapsed)
{
    const auto index = categoryIndex(id);
    if (!m_writable || index < 0 || m_categories.at(index).collapsed == collapsed) {
        return false;
    }
    m_categories[index].collapsed = collapsed;
    return true;
}

QString ModCategoryStore::categoryFor(const Mod& mod) const
{
    return categoryForAliases(aliasesFor(mod));
}

QString ModCategoryStore::categoryForAliases(const QStringList& aliases) const
{
    const auto normalized = normalizeAliases(aliases);
    for (const auto& alias : normalized) {
        for (const auto& assignment : m_assignments) {
            if (assignment.aliases.contains(alias)) {
                return assignment.categoryId;
            }
        }
    }
    return {};
}

bool ModCategoryStore::assign(const Mod& mod, const QString& categoryId)
{
    return assignAliases(aliasesFor(mod), categoryId);
}

bool ModCategoryStore::assignAliases(const QStringList& aliases, const QString& categoryId)
{
    if (!m_writable || (!categoryId.isEmpty() && !containsCategory(categoryId))) {
        return false;
    }

    const auto normalized = normalizeAliases(aliases);
    if (normalized.isEmpty()) {
        return false;
    }

    QList<int> matches;
    for (int i = 0; i < m_assignments.size(); ++i) {
        for (const auto& alias : normalized) {
            if (m_assignments.at(i).aliases.contains(alias)) {
                matches.append(i);
                break;
            }
        }
    }

    if (categoryId.isEmpty()) {
        for (auto i = matches.crbegin(); i != matches.crend(); ++i) {
            m_assignments.removeAt(*i);
        }
        return !matches.isEmpty();
    }

    Assignment merged{ categoryId, normalized };
    for (const auto index : matches) {
        merged.aliases.append(m_assignments.at(index).aliases);
    }
    merged.aliases = normalizeAliases(merged.aliases);
    if (matches.size() == 1) {
        const auto& existing = m_assignments.at(matches.constFirst());
        if (existing.categoryId == merged.categoryId && existing.aliases == merged.aliases) {
            return false;
        }
    }
    for (auto i = matches.crbegin(); i != matches.crend(); ++i) {
        m_assignments.removeAt(*i);
    }
    m_assignments.append(merged);
    return true;
}
