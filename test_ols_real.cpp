#include <gtest/gtest.h>
#include "ecu/WinolsParser.hpp"
#include "ecu/OlsImport.hpp"
#include <QFile>
#include <QByteArray>
#include <QString>

using namespace ecu;

TEST(RealOlsFile, WinolsParserTreatsNativeOlsAsBinary) {
    QFile f("tests/ols_import/RENAULT_2.0_DCI_EDC16CP33.ols");
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    f.close();
    
    EXPECT_TRUE(data.startsWith("\x0b\x00\x00\x00WinOLS File"));
    
    WinolsParser parser;
    auto result = parser.parse(data, "test.ols");
    
    ASSERT_TRUE(result.has_value()) << result.error().toStdString();
    EXPECT_EQ(result->rom.size(), data.size());
    EXPECT_EQ(result->maps.size(), 0);
}

TEST(RealOlsFile, OlsImportExtractsRom) {
    auto romResult = olsExtractRom(QString("tests/ols_import/RENAULT_2.0_DCI_EDC16CP33.ols"));
    ASSERT_TRUE(romResult.has_value()) << romResult.error();
    const QByteArray rom = *romResult;
    EXPECT_LT(rom.size(), 10000000);
    EXPECT_GT(rom.size(), 0x40000);
}

TEST(RealOlsFile, OlsImportExtractsMaps) {
    auto mapsResult = olsExtractMaps(QString("tests/ols_import/RENAULT_2.0_DCI_EDC16CP33.ols"));
    ASSERT_TRUE(mapsResult.has_value()) << mapsResult.error();
    EXPECT_GT(mapsResult->size(), 0);
}
