// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class Mod;

class ModCategoryStore {
   public:
    struct Category {
        QString id;
        QString name;
        bool collapsed = false;
    };

    explicit ModCategoryStore(QString instanceRoot, QString scope);

    static QString relativePath();
    static QStringList aliasesFor(const Mod& mod);

    bool load();
    bool save() const;
    bool isWritable() const { return m_writable; }

    const QList<Category>& categories() const { return m_categories; }
    QString categoryFor(const Mod& mod) const;
    QString categoryForAliases(const QStringList& aliases) const;

    QString addCategory(const QString& name);
    bool renameCategory(const QString& id, const QString& name);
    bool removeCategory(const QString& id);
    bool moveCategory(const QString& id, int offset);
    bool setCollapsed(const QString& id, bool collapsed);
    bool assign(const Mod& mod, const QString& categoryId);
    bool assignAliases(const QStringList& aliases, const QString& categoryId);

    bool containsCategory(const QString& id) const;
    QString categoryName(const QString& id) const;

   private:
    struct Assignment {
        QString categoryId;
        QStringList aliases;
    };

    static QString normalizeAlias(const QString& alias);
    static QStringList normalizeAliases(const QStringList& aliases);
    int categoryIndex(const QString& id) const;
    bool nameAvailable(const QString& name, const QString& exceptId = {}) const;

    QString m_instanceRoot;
    QString m_scope;
    QList<Category> m_categories;
    QList<Assignment> m_assignments;
    bool m_writable = true;
};
