// SPDX-License-Identifier: GPL-3.0-only

#include "ModCategoryProxyModel.h"

#include <QAbstractProxyModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QSet>
#include <QSize>
#include <QSortFilterProxyModel>

#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModFolderModel.h"

ModCategoryProxyModel::ModCategoryProxyModel(QString instanceRoot, QString scope, ModFolderModel* modModel, QObject* parent)
    : QAbstractProxyModel(parent), m_modModel(modModel), m_store(std::move(instanceRoot), std::move(scope))
{
    if (!m_store.load()) {
        m_lastError = tr("The category file could not be loaded. It will not be overwritten.");
    }
}

QModelIndex ModCategoryProxyModel::index(int row, int column, const QModelIndex& parent) const
{
    if (parent.isValid() || row < 0 || row >= m_rows.size() || column < 0 || column >= columnCount()) {
        return {};
    }
    return createIndex(row, column);
}

QModelIndex ModCategoryProxyModel::parent([[maybe_unused]] const QModelIndex& child) const
{
    return {};
}

int ModCategoryProxyModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int ModCategoryProxyModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() || !sourceModel() ? 0 : sourceModel()->columnCount();
}

bool ModCategoryProxyModel::hasChildren(const QModelIndex& parent) const
{
    return !parent.isValid() && !m_rows.isEmpty();
}

QModelIndex ModCategoryProxyModel::mapFromSource(const QModelIndex& sourceIndex) const
{
    if (!sourceIndex.isValid() || sourceIndex.model() != sourceModel()) {
        return {};
    }
    const auto sourceColumnZero = sourceIndex.siblingAtColumn(0);
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).sourceIndex == sourceColumnZero) {
            return index(row, sourceIndex.column());
        }
    }
    return {};
}

QModelIndex ModCategoryProxyModel::mapToSource(const QModelIndex& proxyIndex) const
{
    if (!proxyIndex.isValid() || proxyIndex.model() != this || proxyIndex.row() < 0 || proxyIndex.row() >= m_rows.size()) {
        return {};
    }
    const auto& row = m_rows.at(proxyIndex.row());
    if (!row.sourceIndex.isValid()) {
        return {};
    }
    return QModelIndex(row.sourceIndex).siblingAtColumn(proxyIndex.column());
}

QVariant ModCategoryProxyModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }

    const auto& row = m_rows.at(index.row());
    if (row.isCategory()) {
        switch (role) {
            case CategoryHeaderRole:
                return true;
            case CategoryIdRole:
                return row.categoryId;
            case CategoryNameRole:
                return row.categoryName;
            case CategoryCollapsedRole:
                return row.collapsed && !m_filterActive;
            case CategoryCountRole:
                return row.count;
            case Qt::DisplayRole:
            case Qt::AccessibleTextRole:
                return index.column() == 0 ? tr("%1 (%2)").arg(row.categoryName).arg(row.count) : QVariant{};
            case Qt::ToolTipRole:
                return m_filterActive ? tr("Matching mods are temporarily expanded while searching.")
                                      : tr("Click to expand or collapse this category. Drop installed mods here to assign them.");
            case Qt::SizeHintRole:
                return QSize(0, 32);
            default:
                return {};
        }
    }

    const auto sourceIndex = mapToSource(index);
    return sourceIndex.isValid() ? sourceModel()->data(sourceIndex, role) : QVariant{};
}

bool ModCategoryProxyModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    const auto sourceIndex = mapToSource(index);
    return sourceIndex.isValid() && sourceModel()->setData(sourceIndex, value, role);
}

QVariant ModCategoryProxyModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    return sourceModel() ? sourceModel()->headerData(section, orientation, role) : QVariant{};
}

Qt::ItemFlags ModCategoryProxyModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::ItemIsDropEnabled;
    }
    if (isCategory(index)) {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
    }
    const auto sourceIndex = mapToSource(index);
    return sourceIndex.isValid() ? sourceModel()->flags(sourceIndex) | Qt::ItemIsDragEnabled : Qt::NoItemFlags;
}

QStringList ModCategoryProxyModel::mimeTypes() const
{
    auto types = sourceModel() ? sourceModel()->mimeTypes() : QStringList{};
    if (!types.contains(QLatin1String(MIME_TYPE))) {
        types.append(QLatin1String(MIME_TYPE));
    }
    return types;
}

QMimeData* ModCategoryProxyModel::mimeData(const QModelIndexList& indexes) const
{
    auto data = new QMimeData;
    QJsonArray resources;
    QSet<int> seenRows;
    for (const auto& index : indexes) {
        if (!index.isValid() || isCategory(index) || seenRows.contains(index.row())) {
            continue;
        }
        seenRows.insert(index.row());
        const auto sourceIndex = mapToSource(index.siblingAtColumn(0));
        if (const auto mod = modForSource(sourceIndex)) {
            QJsonArray aliases;
            for (const auto& alias : aliasesForMod(*mod)) {
                aliases.append(alias);
            }
            resources.append(aliases);
        }
    }
    if (!resources.isEmpty()) {
        data->setData(QLatin1String(MIME_TYPE), QJsonDocument(resources).toJson(QJsonDocument::Compact));
    }
    return data;
}

bool ModCategoryProxyModel::canDropMimeData(const QMimeData* data,
                                            Qt::DropAction action,
                                            int row,
                                            [[maybe_unused]] int column,
                                            const QModelIndex& parent) const
{
    if (action == Qt::IgnoreAction) {
        return true;
    }
    if (!data) {
        return false;
    }
    QModelIndex target = parent;
    if (!target.isValid() && row >= 0 && row < rowCount()) {
        target = index(row, 0);
    }
    if (data->hasFormat(QLatin1String(MIME_TYPE))) {
        return isCategory(target);
    }
    if (data->hasUrls()) {
        return !isCategory(target);
    }
    return false;
}

bool ModCategoryProxyModel::dropMimeData(const QMimeData* data,
                                         Qt::DropAction action,
                                         int row,
                                         int column,
                                         const QModelIndex& parent)
{
    if (action == Qt::IgnoreAction) {
        return true;
    }
    if (!data) {
        return false;
    }

    QModelIndex target = parent;
    if (!target.isValid() && row >= 0 && row < rowCount()) {
        target = index(row, 0);
    }
    if (data->hasFormat(QLatin1String(MIME_TYPE)) && isCategory(target)) {
        const auto document = QJsonDocument::fromJson(data->data(QLatin1String(MIME_TYPE)));
        if (!document.isArray()) {
            return false;
        }
        m_lastError.clear();
        const auto previous = m_store;
        bool changed = false;
        for (const auto resource : document.array()) {
            QStringList aliases;
            for (const auto alias : resource.toArray()) {
                aliases.append(alias.toString());
            }
            changed |= m_store.assignAliases(aliases, categoryId(target));
        }
        if (!changed) {
            return false;
        }
        return commit(previous);
    }

    if (data->hasUrls() && m_modModel) {
        return m_modModel->dropMimeData(data, action, row, column, {});
    }
    return false;
}

Qt::DropActions ModCategoryProxyModel::supportedDropActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

void ModCategoryProxyModel::sort(int column, Qt::SortOrder order)
{
    if (sourceModel()) {
        sourceModel()->sort(column, order);
    }
}

void ModCategoryProxyModel::setSourceModel(QAbstractItemModel* model)
{
    if (model == sourceModel()) {
        return;
    }
    if (sourceModel()) {
        disconnect(sourceModel(), nullptr, this, nullptr);
    }
    m_rows.clear();
    QAbstractProxyModel::setSourceModel(model);

    if (!model) {
        return;
    }
    const auto reset = [this] { rebuild(); };
    connect(model, &QAbstractItemModel::modelReset, this, reset);
    connect(model, &QAbstractItemModel::rowsInserted, this, reset);
    connect(model, &QAbstractItemModel::rowsRemoved, this, reset);
    connect(model, &QAbstractItemModel::layoutChanged, this, reset);
    connect(model, &QAbstractItemModel::dataChanged, this, reset);
    connect(model, &QObject::destroyed, this, [this] {
        beginResetModel();
        m_rows.clear();
        m_aliasCache.clear();
        endResetModel();
    });
    rebuild();
}

bool ModCategoryProxyModel::isCategory(const QModelIndex& index) const
{
    return index.isValid() && index.model() == this && index.row() >= 0 && index.row() < m_rows.size() &&
           m_rows.at(index.row()).isCategory();
}

QString ModCategoryProxyModel::categoryId(const QModelIndex& index) const
{
    return isCategory(index) ? m_rows.at(index.row()).categoryId : QString{};
}

QString ModCategoryProxyModel::categoryFor(const Mod& mod) const
{
    return m_store.categoryForAliases(aliasesForMod(mod));
}

QString ModCategoryProxyModel::addCategory(const QString& name)
{
    m_lastError.clear();
    const auto previous = m_store;
    const auto id = m_store.addCategory(name);
    if (id.isEmpty()) {
        if (!m_store.isWritable()) {
            m_lastError = tr("The category file is read-only because it could not be loaded.");
        }
        return {};
    }
    return commit(previous) ? id : QString{};
}

bool ModCategoryProxyModel::renameCategory(const QString& id, const QString& name)
{
    m_lastError.clear();
    const auto previous = m_store;
    if (!m_store.renameCategory(id, name)) {
        if (!m_store.isWritable()) {
            m_lastError = tr("The category file is read-only because it could not be loaded.");
        }
        return false;
    }
    return commit(previous);
}

bool ModCategoryProxyModel::removeCategory(const QString& id)
{
    m_lastError.clear();
    const auto previous = m_store;
    return m_store.removeCategory(id) && commit(previous);
}

bool ModCategoryProxyModel::moveCategory(const QString& id, int offset)
{
    m_lastError.clear();
    const auto previous = m_store;
    return m_store.moveCategory(id, offset) && commit(previous);
}

bool ModCategoryProxyModel::toggleCategory(const QModelIndex& index)
{
    if (!isCategory(index) || m_filterActive) {
        return false;
    }
    m_lastError.clear();
    const auto previous = m_store;
    const auto& row = m_rows.at(index.row());
    return m_store.setCollapsed(row.categoryId, !row.collapsed) && commit(previous);
}

bool ModCategoryProxyModel::assignMods(const QList<Mod*>& mods, const QString& categoryId)
{
    if (mods.isEmpty()) {
        return false;
    }
    m_lastError.clear();
    const auto previous = m_store;
    bool changed = false;
    for (const auto mod : mods) {
        if (mod) {
            changed |= m_store.assignAliases(aliasesForMod(*mod), categoryId);
        }
    }
    return changed && commit(previous);
}

void ModCategoryProxyModel::setFilterActive(bool active)
{
    if (m_filterActive == active) {
        return;
    }
    m_filterActive = active;
    rebuild();
}

QModelIndex ModCategoryProxyModel::mapSourceToMod(const QModelIndex& sourceIndex) const
{
    auto current = sourceIndex;
    while (current.isValid() && current.model() != m_modModel) {
        const auto proxy = qobject_cast<const QAbstractProxyModel*>(current.model());
        if (!proxy) {
            return {};
        }
        current = proxy->mapToSource(current);
    }
    return current.model() == m_modModel ? current : QModelIndex{};
}

Mod* ModCategoryProxyModel::modForSource(const QModelIndex& sourceIndex) const
{
    const auto modIndex = mapSourceToMod(sourceIndex);
    if (!m_modModel || !modIndex.isValid() || modIndex.row() < 0 || modIndex.row() >= m_modModel->rowCount()) {
        return nullptr;
    }
    return &m_modModel->at(modIndex.row());
}

QStringList ModCategoryProxyModel::aliasesForMod(const Mod& mod) const
{
    const auto cached = m_aliasCache.constFind(&mod);
    if (cached != m_aliasCache.constEnd()) {
        return cached.value();
    }
    return ModCategoryStore::aliasesFor(mod);
}

void ModCategoryProxyModel::refreshAliasCache()
{
    m_aliasCache.clear();
    if (!m_modModel) {
        return;
    }

    QHash<QString, int> stableAliasCounts;
    QHash<const Mod*, QStringList> rawAliases;
    const auto mods = m_modModel->allMods();
    for (const auto mod : mods) {
        const auto aliases = ModCategoryStore::aliasesFor(*mod);
        rawAliases.insert(mod, aliases);
        for (const auto& alias : aliases) {
            if (!alias.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
                stableAliasCounts[alias] += 1;
            }
        }
    }

    for (const auto mod : mods) {
        auto aliases = rawAliases.value(mod);
        for (auto i = aliases.size() - 1; i >= 0; --i) {
            const auto& alias = aliases.at(i);
            if (!alias.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive) && stableAliasCounts.value(alias) > 1) {
                aliases.removeAt(i);
            }
        }
        m_aliasCache.insert(mod, aliases);
    }
}

void ModCategoryProxyModel::rebuild()
{
    refreshAliasCache();

    if (m_modModel && m_store.isWritable()) {
        const auto previous = m_store;
        bool aliasesChanged = false;
        for (const auto mod : m_modModel->allMods()) {
            const auto aliases = aliasesForMod(*mod);
            const auto category = m_store.categoryForAliases(aliases);
            if (!category.isEmpty()) {
                aliasesChanged |= m_store.assignAliases(aliases, category);
            }
        }
        if (aliasesChanged && !m_store.save()) {
            m_store = previous;
            m_lastError = tr("Failed to save parsed mod identities to the category file.");
        }
    }

    QList<Row> rows;
    if (sourceModel()) {
        QHash<QString, QList<QPersistentModelIndex>> categorized;
        QList<QPersistentModelIndex> uncategorized;
        for (int sourceRow = 0; sourceRow < sourceModel()->rowCount(); ++sourceRow) {
            const auto sourceIndex = sourceModel()->index(sourceRow, 0);
            const auto mod = modForSource(sourceIndex);
            const auto category = mod ? m_store.categoryForAliases(aliasesForMod(*mod)) : QString{};
            if (category.isEmpty() || !m_store.containsCategory(category)) {
                uncategorized.append(sourceIndex);
            } else {
                categorized[category].append(sourceIndex);
            }
        }

        for (const auto& sourceIndex : uncategorized) {
            rows.append({ {}, {}, false, 0, sourceIndex });
        }
        for (const auto& category : m_store.categories()) {
            const auto mods = categorized.value(category.id);
            if (m_filterActive && mods.isEmpty()) {
                continue;
            }
            rows.append({ category.id, category.name, category.collapsed, static_cast<int>(mods.size()), {} });
            if (!category.collapsed || m_filterActive) {
                for (const auto& sourceIndex : mods) {
                    rows.append({ {}, {}, false, 0, sourceIndex });
                }
            }
        }
    }

    bool sameLayout = rows.size() == m_rows.size();
    for (int i = 0; sameLayout && i < rows.size(); ++i) {
        const auto& oldRow = m_rows.at(i);
        const auto& newRow = rows.at(i);
        sameLayout = oldRow.categoryId == newRow.categoryId && oldRow.sourceIndex == newRow.sourceIndex;
    }

    if (sameLayout) {
        m_rows = std::move(rows);
        if (!m_rows.isEmpty() && columnCount() > 0) {
            emit dataChanged(index(0, 0), index(m_rows.size() - 1, columnCount() - 1));
        }
        return;
    }

    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

bool ModCategoryProxyModel::commit(const ModCategoryStore& previous)
{
    if (!m_store.save()) {
        m_store = previous;
        m_lastError = tr("Failed to save mod categories.");
        rebuild();
        return false;
    }
    m_lastError.clear();
    rebuild();
    return true;
}
