#include <gtest/gtest.h>

#include "ecu/DamosImport.hpp"
#include "ecu/A2lParser.hpp"
#include "ecu/OpenDamos.hpp"

#include <QByteArray>
#include <QByteArrayView>
#include <QTemporaryFile>

using namespace ecu;

namespace {

void putU16BE(QByteArray& b, int off, uint16_t v) {
    b[off]     = static_cast<char>((v >> 8) & 0xFF);
    b[off + 1] = static_cast<char>(v & 0xFF);
}

Characteristic makeCurve() {
    Characteristic c;
    c.name      = "RPM_Axis_Test";
    c.longIdentifier = "test curve";
    c.type      = "CURVE";
    c.address   = 0x100;
    c.nx        = 3;
    c.ny        = 0;
    c.dataType  = "UWORD";
    c.byteOrder = "BIG_ENDIAN";
    c.factor    = 1.0f;
    c.unit      = "rpm";
    A2lAxis ax;
    ax.attribute     = "STD_AXIS";
    ax.inputQuantity = "n";
    ax.maxAxisPoints = 3;
    ax.dataType      = "UWORD";
    ax.unit          = "rpm";
    c.axisDefs.append(ax);
    return c;
}

} // namespace

// Inline CURVE : en-tête (1 champ taille élément) puis nx valeurs d'axe.
TEST(DamosImport, InlineCurveFingerprint) {
    Characteristic c = makeCurve();
    QByteArray rom(0x200, '\0');
    // CURVE → hdr = sz (UWORD=2). xo = 0x100 + 2 = 0x102.
    putU16BE(rom, 0x100, 3);        // header (nx)
    putU16BE(rom, 0x102, 800);
    putU16BE(rom, 0x104, 1600);
    putU16BE(rom, 0x106, 3200);

    DamosImportStats st;
    DamosRecipe r = damosToOpenDamos({c}, QByteArrayView(rom.constData(), rom.size()),
                                     "test", &st);

    ASSERT_EQ(r.characteristics.size(), 1u);
    EXPECT_EQ(st.converted, 1);
    const DamosEntry& e = r.characteristics[0];
    EXPECT_EQ(e.type, DamosType::Curve);
    EXPECT_EQ(e.dims.nx, 3);
    EXPECT_EQ(e.defaultAddress, "0x100");
    ASSERT_EQ(e.axes.size(), 1u);
    ASSERT_EQ(e.axes[0].fingerprint.size(), 3u);
    EXPECT_EQ(e.axes[0].fingerprint[0], 800);
    EXPECT_EQ(e.axes[0].fingerprint[1], 1600);
    EXPECT_EQ(e.axes[0].fingerprint[2], 3200);
    EXPECT_EQ(e.axes[0].dataType, DamosDataType::UWordBE);
}

// COM_AXIS : chaque axe lu à sa propre adresse de bloc.
TEST(DamosImport, ComAxisFingerprint) {
    Characteristic c;
    c.name      = "Map_ComAxis";
    c.type      = "MAP";
    c.address   = 0x10;
    c.nx        = 2;
    c.ny        = 2;
    c.dataType  = "UWORD";
    c.byteOrder = "BIG_ENDIAN";
    for (uint32_t addr : {0x300u, 0x320u}) {
        A2lAxis ax;
        ax.attribute     = "COM_AXIS";
        ax.maxAxisPoints = 2;
        ax.dataType      = "UWORD";
        ax.address       = addr;
        c.axisDefs.append(ax);
    }

    QByteArray rom(0x400, '\0');
    putU16BE(rom, 0x300, 1000);
    putU16BE(rom, 0x302, 2000);
    putU16BE(rom, 0x320, 10);
    putU16BE(rom, 0x322, 20);

    DamosRecipe r = damosToOpenDamos({c}, QByteArrayView(rom.constData(), rom.size()),
                                     "test");
    ASSERT_EQ(r.characteristics.size(), 1u);
    const DamosEntry& e = r.characteristics[0];
    EXPECT_TRUE(e.comAxis);
    ASSERT_EQ(e.axes.size(), 2u);
    EXPECT_EQ(e.axes[0].fingerprint, (std::vector<int64_t>{1000, 2000}));
    EXPECT_EQ(e.axes[1].fingerprint, (std::vector<int64_t>{10, 20}));
    ASSERT_TRUE(e.axes[0].address.has_value());
    EXPECT_EQ(*e.axes[0].address, 0x300);
}

// Adresse hors ROM → ignorée proprement (pas de crash, comptée dans skipped).
TEST(DamosImport, OutOfBoundsSkipped) {
    Characteristic c = makeCurve();
    c.address = 0x10000;            // au-delà d'une ROM de 0x200
    QByteArray rom(0x200, '\0');

    DamosImportStats st;
    DamosRecipe r = damosToOpenDamos({c}, QByteArrayView(rom.constData(), rom.size()),
                                     "test", &st);
    EXPECT_TRUE(r.characteristics.empty());
    EXPECT_EQ(st.skipped, 1);
    EXPECT_EQ(st.converted, 0);
    EXPECT_FALSE(st.warnings.isEmpty());
}

// Décodage A2L : fichier Latin-1 avec NUL intercalés (export DAMOS++ SunOS) —
// le parser doit garder la structure intacte (NUL strip + Latin-1).
TEST(DamosImport, A2lDecodesLatin1WithInterleavedNuls) {
    const QByteArray a2lText =
        "ASAP2_VERSION 1 51\n"
        "/begin PROJECT P \"\"\n"
        "/begin MODULE M \"\"\n"
        "/begin CHARACTERISTIC Test_C \"deg \xB0 C\" VALUE 0x1234 RL 0 NO_COMPU_METHOD\n"
        "/end CHARACTERISTIC\n"
        "/end MODULE\n"
        "/end PROJECT\n";

    // Intercale un NUL après chaque octet (comme les exports DAMOS++ SunOS).
    QByteArray interleaved;
    interleaved.reserve(a2lText.size() * 2);
    for (char ch : a2lText) { interleaved.append(ch); interleaved.append('\0'); }

    QTemporaryFile f;
    ASSERT_TRUE(f.open());
    f.write(interleaved);
    f.flush();

    A2lParser p;
    ASSERT_TRUE(p.parse(f.fileName()));
    auto c = p.findByName("Test_C");
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->address, 0x1234u);
}
