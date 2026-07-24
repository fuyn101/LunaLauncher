// SPDX-License-Identifier: GPL-3.0-only

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>
#include <QUrl>

#include "modplatform/flame/FlameAPI.h"

class FlameAPITest : public QObject {
    Q_OBJECT

   private:
    static QJsonObject makeFile(int fileId, const QString& fileName, const QJsonValue& downloadUrl)
    {
        return {
            { "gameVersions", QJsonArray{ "1.20.1", "Forge" } },
            { "modId", 2746 },
            { "id", fileId },
            { "fileDate", "2024-01-01T00:00:00Z" },
            { "displayName", "Test version" },
            { "downloadUrl", downloadUrl },
            { "fileName", fileName },
            { "releaseType", 1 },
            { "hashes", QJsonArray{} },
            { "dependencies", QJsonArray{} },
        };
    }

   private slots:
    void preservesApiDownloadUrl()
    {
        FlameAPI api;
        auto file = makeFile(3509043, "test.jar", "https://example.com/download/test.jar");

        const auto version = api.loadIndexedPackVersion(file, ModPlatform::ResourceType::Mod);

        QCOMPARE(version.downloadUrl, "https://example.com/download/test.jar");
    }

    void createsForgeCdnDownloadUrl()
    {
        FlameAPI api;
        auto file = makeFile(3509043, "screenshot-to-clipboard-1.0.7-fabric.jar", QJsonValue::Null);

        const auto version = api.loadIndexedPackVersion(file, ModPlatform::ResourceType::Mod);

        QCOMPARE(version.downloadUrl,
                 "https://edge.forgecdn.net/files/3509/43/screenshot-to-clipboard-1.0.7-fabric.jar");
    }

    void encodesForgeCdnFileName()
    {
        FlameAPI api;
        const QString fileName = QString::fromUtf8("A+B #100% \xE4\xB8\xAD\xE6\x96\x87.jar");
        auto file = makeFile(1234567, fileName, QJsonValue::Null);

        const auto version = api.loadIndexedPackVersion(file, ModPlatform::ResourceType::Mod);
        const QUrl url(version.downloadUrl);

        QVERIFY(url.isValid());
        QCOMPARE(url.fileName(QUrl::FullyDecoded), fileName);
        QVERIFY(version.downloadUrl.contains("%23"));
        QVERIFY(version.downloadUrl.contains("%25"));
    }

    void leavesInvalidFallbackEmpty()
    {
        FlameAPI api;
        auto file = makeFile(0, "", QJsonValue::Null);

        const auto version = api.loadIndexedPackVersion(file, ModPlatform::ResourceType::Mod);

        QVERIFY(version.downloadUrl.isEmpty());
    }
};

QTEST_GUILESS_MAIN(FlameAPITest)

#include "FlameAPI_test.moc"
