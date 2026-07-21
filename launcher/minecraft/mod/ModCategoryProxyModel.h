// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QAbstractProxyModel>
#include <QHash>
#include <QPersistentModelIndex>
#include <QPointer>

#include "minecraft/mod/ModCategoryStore.h"

class Mod;
class ModFolderModel;

class ModCategoryProxyModel : public QAbstractProxyModel {
    Q_OBJECT

   public:
    enum Roles {
        CategoryHeaderRole = Qt::UserRole + 400,
        CategoryIdRole,
        CategoryNameRole,
        CategoryCollapsedRole,
        CategoryCountRole,
    };

    static constexpr const char* MIME_TYPE = "application/x-lunalauncher-mod-categories";

    ModCategoryProxyModel(QString instanceRoot, QString scope, ModFolderModel* modModel, QObject* parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    bool hasChildren(const QModelIndex& parent = {}) const override;
    QModelIndex mapFromSource(const QModelIndex& sourceIndex) const override;
    QModelIndex mapToSource(const QModelIndex& proxyIndex) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;
    Qt::DropActions supportedDropActions() const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    void setSourceModel(QAbstractItemModel* sourceModel) override;

    bool isCategory(const QModelIndex& index) const;
    QString categoryId(const QModelIndex& index) const;
    QString categoryFor(const Mod& mod) const;
    QString lastError() const { return m_lastError; }
    const QList<ModCategoryStore::Category>& categories() const { return m_store.categories(); }

    QString addCategory(const QString& name);
    bool renameCategory(const QString& id, const QString& name);
    bool removeCategory(const QString& id);
    bool moveCategory(const QString& id, int offset);
    bool toggleCategory(const QModelIndex& index);
    bool assignMods(const QList<Mod*>& mods, const QString& categoryId);
    void setFilterActive(bool active);

   private:
    struct Row {
        QString categoryId;
        QString categoryName;
        bool collapsed = false;
        int count = 0;
        QPersistentModelIndex sourceIndex;

        bool isCategory() const { return !categoryId.isEmpty() && !sourceIndex.isValid(); }
    };

    QModelIndex mapSourceToMod(const QModelIndex& sourceIndex) const;
    Mod* modForSource(const QModelIndex& sourceIndex) const;
    QStringList aliasesForMod(const Mod& mod) const;
    void refreshAliasCache();
    void rebuild();
    bool commit(const ModCategoryStore& previous);

    QPointer<ModFolderModel> m_modModel;
    ModCategoryStore m_store;
    QList<Row> m_rows;
    QHash<const Mod*, QStringList> m_aliasCache;
    bool m_filterActive = false;
    QString m_lastError;
};
