// SPDX-License-Identifier: GPL-3.0-only

#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "translations/POTranslator.h"
#include "translations/TranslationsModel.h"

class TranslationsTest : public QObject {
    Q_OBJECT

   private slots:
    void contextlessEntriesAreFallbacks()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = temp.filePath("test.po");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray content = R"(msgid "Value"
msgstr "通用"

msgctxt "SpecificPage"
msgid "Value"
msgstr "专用"
)";
        QCOMPARE(file.write(content), content.size());
        file.close();

        POTranslator translator(path);
        QCOMPARE(translator.translate("SpecificPage", "Value", nullptr, -1), QString::fromUtf8("专用"));
        QCOMPARE(translator.translate("OtherPage", "Value", nullptr, -1), QString::fromUtf8("通用"));
    }

    void bundledChineseCatalog()
    {
        const QStringList paths = { QFINDTESTDATA("../launcher/translations/zh.po"),
                                    QFINDTESTDATA("../launcher/exresources/translations/!zh.po") };
        for (const auto& path : paths) {
            QVERIFY(!path.isEmpty());
            POTranslator translator(path);

            QCOMPARE(translator.translate("AuthlibInjectorPage", "Status", nullptr, -1), QString::fromUtf8("状态"));
            QCOMPARE(translator.translate("AuthlibInjectorPage", "BMCLAPI (Recommended for China)", nullptr, -1),
                     QString::fromUtf8("BMCLAPI (推荐国内用户)"));
            QCOMPARE(translator.translate("Aria2Page", "Download Backend", nullptr, -1), QString::fromUtf8("下载后端"));
            QCOMPARE(translator.translate("Task", "Failed to create a temporary directory for aria2.", nullptr, -1),
                     QString::fromUtf8("无法为 aria2 创建临时目录。"));
            QCOMPARE(translator.translate("TerracottaOnlinePanel", "EasyTier crashed (guest)", nullptr, -1),
                     QString::fromUtf8("EasyTier 已崩溃（访客）"));
            QCOMPARE(translator.translate("YukariConnectOnlinePanel", "Export Log", nullptr, -1), QString::fromUtf8("导出日志"));
            QCOMPARE(translator.translate("YukariConnectOnlinePanel", "Failed to connect to YukariConnect server.", nullptr, -1),
                     QString::fromUtf8("连接 YukariConnect 服务器失败。"));
            QCOMPARE(translator.translate("ModFolderPage", "Categories", nullptr, -1), QString::fromUtf8("分类"));
            QCOMPARE(translator.translate("LauncherPage", "Use New UI Layout (Requires Restart)", nullptr, -1),
                     QString::fromUtf8("使用新版界面布局（需要重启）"));
            QCOMPARE(translator.translate("LauncherPage", "Show Server Preview in toolbar", nullptr, -1),
                     QString::fromUtf8("在工具栏中显示服务器预览"));
            QCOMPARE(translator.translate("APIPage", "Download &Mirrors", nullptr, -1), QString::fromUtf8("下载镜像(&M)"));
            QCOMPARE(translator.translate("APIPage", "Official", nullptr, -1), QString::fromUtf8("官方"));
            QCOMPARE(translator.translate("APIPage", "Libraries Server", nullptr, -1), QString::fromUtf8("库服务器"));
            QCOMPARE(translator.translate("APIPage", "Mojang Downloads Mirror", nullptr, -1),
                     QString::fromUtf8("Mojang 下载镜像"));
        }
    }

    void poFileNameMapsToLanguageCode()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        QFile index(temp.filePath("index_v2.json"));
        QVERIFY(index.open(QIODevice::WriteOnly));
        const QByteArray indexData = R"({"file_type":"MMC-TRANSLATION-INDEX","version":2,"languages":{}})";
        QCOMPARE(index.write(indexData), indexData.size());
        index.close();

        QFile po(temp.filePath("zh.po"));
        QVERIFY(po.open(QIODevice::WriteOnly));
        const QByteArray poData = "msgid \"Test\"\nmsgstr \"测试\"\n";
        QCOMPARE(po.write(poData), poData.size());
        po.close();

        TranslationsModel model(temp.path());
        bool found = false;
        for (int row = 0; row < model.rowCount(); ++row) {
            if (model.index(row, 0).data(Qt::UserRole).toString() == QStringLiteral("zh")) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }
};

QTEST_GUILESS_MAIN(TranslationsTest)

#include "Translations_test.moc"
