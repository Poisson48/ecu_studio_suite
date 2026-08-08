#include <gtest/gtest.h>
#include "updater.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

using ecu_drive::Updater;

TEST(DriveUpdater, IsNewerSemver) {
    EXPECT_TRUE(Updater::isNewer("v0.5.0", "0.4.0"));
    EXPECT_TRUE(Updater::isNewer("0.4.1", "0.4.0"));
    EXPECT_TRUE(Updater::isNewer("1.0.0", "0.9.9"));
    EXPECT_TRUE(Updater::isNewer("0.10.0", "0.9.1"));
    EXPECT_FALSE(Updater::isNewer("0.4.0", "0.4.0"));
    EXPECT_FALSE(Updater::isNewer("v0.4.0", "0.4.0"));
    EXPECT_FALSE(Updater::isNewer("0.3.9", "0.4.0"));
    EXPECT_FALSE(Updater::isNewer("0.9.1", "0.10.0"));
    EXPECT_FALSE(Updater::isNewer("", "0.4.0"));
    EXPECT_FALSE(Updater::isNewer("0.4", "0.4.0"));
    EXPECT_TRUE(Updater::isNewer("0.4.1", "0.4"));
}

TEST(DriveUpdater, NotesFromBodyStopsAtSeparator) {
    const QString body =
        QStringLiteral("## Nouveautés\n- fix BT\n- maj auto\n---\nChecksums…\n");
    const QString notes = Updater::notesFromBody(body);
    EXPECT_TRUE(notes.contains(QStringLiteral("fix BT")));
    EXPECT_FALSE(notes.contains(QStringLiteral("Checksums")));
    EXPECT_EQ(Updater::notesFromBody(QStringLiteral("Juste un correctif.")),
              QStringLiteral("Juste un correctif."));
    EXPECT_TRUE(Updater::notesFromBody(QString()).isEmpty());
}

TEST(DriveUpdater, PickApkPrefersEcuDriveAsset) {
    const QByteArray json = R"([
      {"name":"ECU_Studio-x86_64.AppImage","browser_download_url":"https://example/app.AppImage"},
      {"name":"other.apk","browser_download_url":"https://example/other.apk"},
      {"name":"ecu-drive-v1.7.6-arm64.apk","browser_download_url":"https://example/ecu-drive.apk"}
    ])";
    const QJsonArray assets = QJsonDocument::fromJson(json).array();
    EXPECT_EQ(Updater::pickApkAssetUrl(assets),
              QStringLiteral("https://example/ecu-drive.apk"));
}

TEST(DriveUpdater, PickApkFallsBackToAnyApk) {
    const QByteArray json = R"([
      {"name":"release-manifest.json","browser_download_url":"https://example/m.json"},
      {"name":"nightly.apk","browser_download_url":"https://example/nightly.apk"}
    ])";
    const QJsonArray assets = QJsonDocument::fromJson(json).array();
    EXPECT_EQ(Updater::pickApkAssetUrl(assets),
              QStringLiteral("https://example/nightly.apk"));
}

TEST(DriveUpdater, PickApkEmptyWhenNone) {
    const QByteArray json = R"([
      {"name":"ECU_Studio-x86_64.AppImage","browser_download_url":"https://example/app.AppImage"}
    ])";
    EXPECT_TRUE(Updater::pickApkAssetUrl(QJsonDocument::fromJson(json).array()).isEmpty());
}

TEST(DriveUpdater, CheckFindsLatestReleaseFromGitHub) {
    // Intégration légère : vérifie que l'API Releases répond et que le parseur
    // trouve un APK ecu-drive sur la dernière release stable.
    QNetworkAccessManager nam;
    QNetworkRequest req{
        QUrl(QStringLiteral(
            "https://api.github.com/repos/Poisson48/ecu_studio_suite/releases?per_page=5"))};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "ECU-Drive-Test");

    QNetworkReply* reply = nam.get(req);
    QEventLoop loop;
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    ASSERT_EQ(reply->error(), QNetworkReply::NoError)
        << reply->errorString().toStdString();
    const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
    reply->deleteLater();
    ASSERT_FALSE(arr.isEmpty());

    bool foundApk = false;
    QString newest;
    for (const QJsonValue& v : arr) {
        const QJsonObject obj = v.toObject();
        if (obj.value(QStringLiteral("draft")).toBool()) continue;
        if (obj.value(QStringLiteral("prerelease")).toBool()) continue;
        QString ver = obj.value(QStringLiteral("tag_name")).toString();
        if (ver.startsWith(QLatin1Char('v'))) ver.remove(0, 1);
        if (newest.isEmpty() || Updater::isNewer(ver, newest))
            newest = ver;
        const QString apk = Updater::pickApkAssetUrl(
            obj.value(QStringLiteral("assets")).toArray());
        if (!apk.isEmpty()) {
            foundApk = true;
            EXPECT_TRUE(apk.contains(QStringLiteral(".apk")));
            EXPECT_TRUE(apk.contains(QStringLiteral("ecu-drive"))
                        || apk.contains(QStringLiteral("ecu_drive")));
        }
    }
    EXPECT_FALSE(newest.isEmpty());
    EXPECT_TRUE(foundApk) << "Aucune release récente avec APK ecu-drive";
}

TEST(DriveUpdater, CheckSlotTransitionsIdleOrAvailable) {
    Updater updater;
    EXPECT_EQ(updater.state(), Updater::Idle);
    EXPECT_EQ(updater.currentVersion(), QStringLiteral("1.7.0-test"));

    QEventLoop loop;
    QObject::connect(&updater, &Updater::stateChanged, &loop, [&]() {
        if (updater.state() != Updater::Checking)
            loop.quit();
    });
    QTimer::singleShot(25000, &loop, &QEventLoop::quit);
    updater.check();
    loop.exec();

    const auto st = updater.state();
    EXPECT_TRUE(st == Updater::Idle || st == Updater::Available || st == Updater::Failed)
        << "état inattendu: " << int(st);
    if (st == Updater::Failed) {
        // Réseau CI parfois restreint — on documente l'erreur sans faire
        // échouer le build si le parseur unitaire ci-dessus a déjà passé.
        GTEST_SKIP() << updater.lastError().toStdString();
    }
    if (st == Updater::Available) {
        EXPECT_TRUE(Updater::isNewer(updater.latestVersion(), updater.currentVersion()));
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
