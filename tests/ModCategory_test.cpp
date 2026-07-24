// SPDX-License-Identifier: GPL-3.0-only

#include <QFile>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>

#include "QObjectPtr.h"
#include "minecraft/mod/Mod.h"
#include "minecraft/mod/ModCategoryProxyModel.h"
#include "minecraft/mod/ModCategoryStore.h"
#include "minecraft/mod/ModFolderModel.h"

class TestModFolderModel : public ModFolderModel {
   public:
    explicit TestModFolderModel(const QDir& dir) : ModFolderModel(dir, nullptr, false, true) {}

    Mod* addMod(const QString& path, const QString& modId)
    {
        const auto row = m_resources.size();
        beginInsertRows({}, row, row);
        auto mod = makeShared<Mod>(QFileInfo(path));
        ModDetails details;
        details.mod_id = modId;
        details.name = QFileInfo(path).completeBaseName();
        mod->setDetails(details);
        m_resources_index.insert(mod->internal_id(), row);
        m_resources.append(mod);
        endInsertRows();
        return mod.get();
    }

    void setModId(Mod* mod, const QString& modId)
    {
        for (int row = 0; row < m_resources.size(); ++row) {
            if (m_resources.at(row).get() != mod) {
                continue;
            }
            auto details = mod->details();
            details.mod_id = modId;
            mod->setDetails(details);
            emit dataChanged(index(row, 0), index(row, columnCount({}) - 1));
            return;
        }
    }
};

class ModCategoryTest : public QObject {
    Q_OBJECT

   private slots:
    void storeRoundTrip()
    {
        QTemporaryDir instance;
        QVERIFY(instance.isValid());
        const auto modsPath = QDir(instance.path()).absoluteFilePath("minecraft/mods");
        QVERIFY(QDir().mkpath(modsPath));
        const auto filePath = QDir(modsPath).absoluteFilePath("example.jar");
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        Mod mod(filePath);
        ModDetails details;
        details.mod_id = "example";
        mod.setDetails(details);

        ModCategoryStore first(instance.path(), "minecraft/mods");
        QVERIFY(first.load());
        const auto category = first.addCategory("Performance");
        QVERIFY(!category.isEmpty());
        QVERIFY(first.assign(mod, category));
        QVERIFY(first.setCollapsed(category, true));
        QVERIFY(first.save());

        ModCategoryStore second(instance.path(), "minecraft/mods");
        QVERIFY(second.load());
        QCOMPARE(second.categories().size(), 1);
        QCOMPARE(second.categories().at(0).name, QString("Performance"));
        QVERIFY(second.categories().at(0).collapsed);
        QCOMPARE(second.categoryFor(mod), category);

        const auto updatedPath = QDir(modsPath).absoluteFilePath("example-2.0.jar");
        QFile updatedFile(updatedPath);
        QVERIFY(updatedFile.open(QIODevice::WriteOnly));
        updatedFile.close();
        Mod updated(updatedPath);
        updated.setDetails(details);
        QCOMPARE(second.categoryFor(updated), category);

        ModCategoryStore otherScope(instance.path(), "coremods");
        QVERIFY(otherScope.load());
        const auto otherCategory = otherScope.addCategory("Legacy");
        QVERIFY(!otherCategory.isEmpty());
        QVERIFY(otherScope.save());

        ModCategoryStore reloaded(instance.path(), "minecraft/mods");
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.categoryFor(updated), category);
        QVERIFY(reloaded.removeCategory(category));
        QCOMPARE(reloaded.categoryFor(updated), QString());
        QVERIFY(reloaded.save());

        ModCategoryStore afterDelete(instance.path(), "minecraft/mods");
        QVERIFY(afterDelete.load());
        QCOMPARE(afterDelete.categories().size(), 0);

        ModCategoryStore preservedScope(instance.path(), "coremods");
        QVERIFY(preservedScope.load());
        QCOMPARE(preservedScope.categories().size(), 1);
        QCOMPARE(preservedScope.categories().at(0).name, QString("Legacy"));
    }

    void invalidStoreIsNotOverwritten()
    {
        QTemporaryDir instance;
        QVERIFY(instance.isValid());
        const auto filePath = QDir(instance.path()).absoluteFilePath(ModCategoryStore::relativePath());
        QVERIFY(QDir().mkpath(QFileInfo(filePath).absolutePath()));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray invalid("not json");
        QCOMPARE(file.write(invalid), invalid.size());
        file.close();

        ModCategoryStore store(instance.path(), "minecraft/mods");
        QVERIFY(!store.load());
        QVERIFY(!store.isWritable());
        QVERIFY(store.addCategory("Unsafe").isEmpty());
        QVERIFY(!store.save());

        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), invalid);
    }

    void invalidStoreSchemaIsNotOverwritten()
    {
        QTemporaryDir instance;
        QVERIFY(instance.isValid());
        const auto filePath = QDir(instance.path()).absoluteFilePath(ModCategoryStore::relativePath());
        QVERIFY(QDir().mkpath(QFileInfo(filePath).absolutePath()));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray invalidSchema(R"({"formatVersion":1,"scopes":{"minecraft/mods":{"categories":{},"assignments":[]}}})");
        QCOMPARE(file.write(invalidSchema), invalidSchema.size());
        file.close();

        ModCategoryStore store(instance.path(), "minecraft/mods");
        QVERIFY(!store.load());
        QVERIFY(!store.isWritable());
        QVERIFY(!store.save());

        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), invalidSchema);
    }

    void proxyEnrichesParsedIdentity()
    {
        QTemporaryDir instance;
        QVERIFY(instance.isValid());
        const auto modsPath = QDir(instance.path()).absoluteFilePath("minecraft/mods");
        QVERIFY(QDir().mkpath(modsPath));
        const auto originalPath = QDir(modsPath).absoluteFilePath("late-1.0.jar");
        QFile originalFile(originalPath);
        QVERIFY(originalFile.open(QIODevice::WriteOnly));
        originalFile.close();

        TestModFolderModel model(modsPath);
        auto mod = model.addMod(originalPath, {});
        auto filter = model.createFilterProxyModel(this);
        filter->setSourceModel(&model);
        ModCategoryProxyModel categories(instance.path(), "minecraft/mods", &model);
        categories.setSourceModel(filter);

        const auto category = categories.addCategory("Late metadata");
        QVERIFY(!category.isEmpty());
        QVERIFY(categories.assignMods({ mod }, category));
        model.setModId(mod, "late-mod");

        const auto updatedPath = QDir(modsPath).absoluteFilePath("late-2.0.jar");
        QFile updatedFile(updatedPath);
        QVERIFY(updatedFile.open(QIODevice::WriteOnly));
        updatedFile.close();
        Mod updated(updatedPath);
        ModDetails details;
        details.mod_id = "late-mod";
        updated.setDetails(details);

        ModCategoryStore reloaded(instance.path(), "minecraft/mods");
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.categoryFor(updated), category);
    }

    void proxyGroupingAndDragDrop()
    {
        QTemporaryDir instance;
        QVERIFY(instance.isValid());
        const auto modsPath = QDir(instance.path()).absoluteFilePath("minecraft/mods");
        QVERIFY(QDir().mkpath(modsPath));

        TestModFolderModel model(modsPath);
        QList<Mod*> mods;
        for (const auto& name : { "alpha", "beta", "gamma" }) {
            const auto modName = QString::fromLatin1(name);
            const auto path = QDir(modsPath).absoluteFilePath(QString("%1.jar").arg(modName));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.close();
            mods.append(model.addMod(path, modName == QStringLiteral("gamma") ? modName : QStringLiteral("shared")));
        }

        auto filter = model.createFilterProxyModel(this);
        filter->setSourceModel(&model);
        ModCategoryProxyModel categories(instance.path(), "minecraft/mods", &model);
        categories.setSourceModel(filter);

        QCOMPARE(model.index(0, ModFolderModel::NameColumn).data(Qt::DisplayRole).toString(), QStringLiteral("alpha"));
        QCOMPARE(filter->index(0, ModFolderModel::NameColumn).data(Qt::DisplayRole).toString(), QStringLiteral("alpha"));
        QCOMPARE(categories.index(0, ModFolderModel::NameColumn).data(Qt::DisplayRole).toString(), QStringLiteral("alpha (alpha.jar)"));

        const auto category = categories.addCategory("Gameplay");
        QVERIFY(!category.isEmpty());
        QCOMPARE(categories.rowCount(), 4);
        QVERIFY(categories.assignMods({ mods.at(1) }, category));
        QCOMPARE(categories.rowCount(), 4);
        QCOMPARE(categories.categoryFor(*mods.at(0)), QString());
        QCOMPARE(categories.categoryFor(*mods.at(1)), category);
        QVERIFY(categories.isCategory(categories.index(2, 0)));
        QCOMPARE(categories.index(2, 0).data(Qt::SizeHintRole).toSize(), QSize(0, 32));
        QCOMPARE(categories.mapToSource(categories.index(0, 0)).row(), 0);
        QVERIFY(!categories.mapToSource(categories.index(2, 0)).isValid());

        QVERIFY(categories.toggleCategory(categories.index(2, 0)));
        QCOMPARE(categories.rowCount(), 3);
        filter->setFilterRegularExpression(QStringLiteral("beta"));
        categories.setFilterActive(true);
        QCOMPARE(categories.rowCount(), 2);
        QVERIFY(categories.isCategory(categories.index(0, 0)));
        QVERIFY(!categories.index(0, 0).data(ModCategoryProxyModel::CategoryCollapsedRole).toBool());
        filter->setFilterRegularExpression(QString());
        categories.setFilterActive(false);
        QCOMPARE(categories.rowCount(), 3);
        QVERIFY(categories.toggleCategory(categories.index(2, 0)));

        const QModelIndexList dragged{ categories.index(0, 0), categories.index(1, 0) };
        std::unique_ptr<QMimeData> mime(categories.mimeData(dragged));
        QVERIFY(mime->hasFormat(ModCategoryProxyModel::MIME_TYPE));
        QVERIFY(categories.canDropMimeData(mime.get(), Qt::MoveAction, -1, -1, categories.index(2, 0)));
        QVERIFY(categories.dropMimeData(mime.get(), Qt::MoveAction, -1, -1, categories.index(2, 0)));
        QCOMPARE(categories.rowCount(), 4);
        QVERIFY(categories.isCategory(categories.index(0, 0)));
        for (int row = 1; row < categories.rowCount(); ++row) {
            QVERIFY(categories.mapToSource(categories.index(row, 0)).isValid());
        }
    }

    void displayNameIncludesDisabledFileSuffix()
    {
        QTemporaryDir instance;
        QVERIFY(instance.isValid());
        const auto modsPath = QDir(instance.path()).absoluteFilePath("minecraft/mods");
        QVERIFY(QDir().mkpath(modsPath));
        const auto filePath = QDir(modsPath).absoluteFilePath("example.jar.disabled");
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        TestModFolderModel model(modsPath);
        auto mod = model.addMod(filePath, "example");
        auto details = mod->details();
        details.name = "Example Mod";
        mod->setDetails(details);

        auto filter = model.createFilterProxyModel(this);
        filter->setSourceModel(&model);
        ModCategoryProxyModel categories(instance.path(), "minecraft/mods", &model);
        categories.setSourceModel(filter);

        QCOMPARE(categories.index(0, ModFolderModel::NameColumn).data(Qt::DisplayRole).toString(),
                 QStringLiteral("Example Mod (example.jar.disabled)"));
    }
};

QTEST_GUILESS_MAIN(ModCategoryTest)

#include "ModCategory_test.moc"
