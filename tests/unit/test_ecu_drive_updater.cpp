#include <gtest/gtest.h>
#include "updater.h"

#include <QCoreApplication>

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

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
