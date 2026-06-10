#include <gtest/gtest.h>

#ifdef ECU_GIT_AVAILABLE
#include "ecu/GitManager.hpp"
#include <QTemporaryDir>
#include <QFile>
#include <cstdint>
#include <string>

// Valide le socle « chaque sauvegarde = un commit récupérable » (auto-commit).
TEST(GitManager, CommitsCreateRecoverableHistory) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    ecu::GitManager git(tmp.path().toStdString());
    ASSERT_TRUE(git.init().has_value());

    auto writeRom = [&](const QByteArray& b) {
        QFile f(tmp.path() + "/rom.bin");
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(b);
    };

    writeRom(QByteArray("\x01\x02\x03\x04", 4));      // état d'origine
    auto c1 = git.commit("v1 (original)");
    ASSERT_TRUE(c1.hash.has_value());

    writeRom(QByteArray("\xAA\x02\x03\x04", 4));      // modification
    auto c2 = git.commit("v2 (tuné)");
    ASSERT_TRUE(c2.hash.has_value());

    EXPECT_GE(git.log().size(), std::size_t{2});      // historique complet

    // Les DEUX états sont intégralement récupérables depuis leurs commits — c'est
    // la garantie « ultra-sécure » : aucun état sauvegardé n'est jamais perdu.
    auto b1 = git.readFileAtCommit(*c1.hash, "rom.bin");
    auto b2 = git.readFileAtCommit(*c2.hash, "rom.bin");
    ASSERT_TRUE(b1.has_value());
    ASSERT_TRUE(b2.has_value());
    ASSERT_EQ(b1->size(), std::size_t{4});
    ASSERT_EQ(b2->size(), std::size_t{4});
    EXPECT_EQ((*b1)[0], 0x01);                          // l'ORIGINAL récupérable
    EXPECT_EQ((*b2)[0], static_cast<std::uint8_t>(0xAA)); // l'état tuné récupérable
}
#else
TEST(GitManager, SkippedNoLibgit2) { GTEST_SKIP() << "libgit2 non compilé"; }
#endif
