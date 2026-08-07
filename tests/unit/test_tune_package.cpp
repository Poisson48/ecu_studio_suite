#include <gtest/gtest.h>
#include "ecu/TunePackage.hpp"
#include "ecu/OpenDamos.hpp"
#include "ecu/TuneValidation.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QDir>

namespace {

QByteArray loadFixtureRecipe() {
    // Cherche ressources/edc16c34/open_damos.json depuis le dépôt.
    QStringList roots;
    roots << QStringLiteral("ressources/edc16c34/open_damos.json");
    roots << QStringLiteral("../ressources/edc16c34/open_damos.json");
    roots << QStringLiteral("../../ressources/edc16c34/open_damos.json");
    roots << QStringLiteral("../../../ressources/edc16c34/open_damos.json");
    const QString app = QCoreApplication::applicationDirPath();
    if (!app.isEmpty()) {
        QDir d(app);
        for (int i = 0; i < 6; ++i) {
            roots << d.filePath(QStringLiteral("ressources/edc16c34/open_damos.json"));
            if (!d.cdUp()) break;
        }
    }
    for (const QString& p : roots) {
        QFile f(p);
        if (f.open(QIODevice::ReadOnly)) return f.readAll();
    }
    return {};
}

} // namespace

TEST(TunePackage, RoundTripZip) {
    const QByteArray recipe = loadFixtureRecipe();
    if (recipe.isEmpty())
        GTEST_SKIP() << "open_damos.json fixture not found";

    QByteArray rom(1024 * 64, '\0');
    // Remplir un peu pour un MD5 stable non trivial
    for (int i = 0; i < rom.size(); ++i) rom[i] = char(i & 0xff);

    const auto pkg = ecu::TunePackageIo::make(
        rom, QStringLiteral("edc16c34"), recipe,
        QStringLiteral("1.7.0-test"), QStringLiteral("unit test"));

    auto zip = ecu::TunePackageIo::writeZip(pkg);
    ASSERT_TRUE(zip.has_value()) << zip.error().toStdString();
    EXPECT_GT(zip->size(), 100);

    auto back = ecu::TunePackageIo::readZip(*zip);
    ASSERT_TRUE(back.has_value()) << back.error().toStdString();
    EXPECT_EQ(back->manifest.ecuId, QStringLiteral("edc16c34"));
    EXPECT_EQ(back->rom, rom);
    EXPECT_EQ(back->recipeJson, recipe);
    EXPECT_EQ(back->notes, QStringLiteral("unit test"));
    EXPECT_FALSE(back->manifest.romMd5.isEmpty());
}

TEST(TunePackage, LoadValidatorFromPackage) {
    const QByteArray recipe = loadFixtureRecipe();
    if (recipe.isEmpty())
        GTEST_SKIP() << "open_damos.json fixture not found";

    // ROM trop petite / sans fingerprints → load peut renvoyer false (pas de rules).
    // On vérifie surtout que parseRecipe + loadRomWithRecipe ne crashent pas.
    QByteArray rom(256 * 1024, char(0xFF));
    auto pkg = ecu::TunePackageIo::make(rom, QStringLiteral("edc16c34"), recipe);

    auto zip = ecu::TunePackageIo::writeZip(pkg);
    ASSERT_TRUE(zip);
    auto back = ecu::TunePackageIo::readZip(*zip);
    ASSERT_TRUE(back);

    ecu::TuneValidator v;
    // Peut être false si aucune map relocalisable — acceptable.
    (void)v.loadTunePackage(*back);
    EXPECT_EQ(v.ecuId(), QStringLiteral("edc16c34"));
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
