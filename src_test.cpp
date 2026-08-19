#include <gtest/gtest.h>
#include "ecu/WinolsParser.hpp"
#include "ecu/OlsImport.hpp"
#include <QFile>
#include <QByteArray>
#include <QString>

using namespace ecu;

TEST(RealOlsFile, WinolsParserTreatsNativeOlsAsBinary) {
    // Load the real WinOLS .ols binary project file
    QFile f("tests/ols_import/RENAULT_2.0_DCI_EDC16CP33.ols");
    ASSERT_TRUE(f.open(QIODevice::ReadOnly)) << "Cannot open test file";
    const QByteArray data = f.readAll();
    f.close();
    
    // Verify file is not empty and starts with WinOLS magic
    EXPECT_GT(data.size(), 0);
    EXPECT_TRUE(data.startsWith("\x0b\x00\x00\x00WinOLS File"));
    
    // WinolsParser::parse should treat it as raw binary (since it's not ZIP, not HEX)
    WinolsParser parser;
    auto result = parser.parse(data, "test.ols");
    
    ASSERT_TRUE(result.has_value()) << result.error().toStdString();
    
    // The result ROM should be the entire file (treated as binary passthrough)
    EXPECT_EQ(result->rom.size(), data.size());
    EXPECT_EQ(result->rom, data);
    
    // Maps list is empty (no metadata extracted from native .ols)
    EXPECT_EQ(result->maps.size(), 0);
}

TEST(RealOlsFile, OlsImportExtractsRomFromNativeOls) {
    // Test the dedicated OlsImport functions
    auto romResult = olsExtractRom(QString("tests/ols_import/RENAULT_2.0_DCI_EDC16CP33.ols"));
    
    ASSERT_TRUE(romResult.has_value()) << romResult.error();
    const QByteArray rom = *romResult;
    
    // ROM should be extracted, not the entire .ols file
    EXPECT_LT(rom.size(), 10000000); // Should be much smaller than 9MB .ols file
    EXPECT_GT(rom.size(), 0x40000); // Min size check from OlsImport
    
    // Should be mostly binary, not the WinOLS magic
    EXPECT_FALSE(rom.startsWith("\x0b\x00\x00\x00WinOLS"));
}

TEST(RealOlsFile, OlsImportExtractsMapsFromNativeOls) {
    // Test the map extraction (best-effort, names only)
    auto mapsResult = olsExtractMaps(QString("tests/ols_import/RENAULT_2.0_DCI_EDC16CP33.ols"));
    
    ASSERT_TRUE(mapsResult.has_value()) << mapsResult.error();
    const auto& maps = *mapsResult;
    
    // Should have extracted some map names
    EXPECT_GT(maps.size(), 0) << "OLS should contain at least some DAMOS labels";
    
    // Check that extracted names look like ECU parameter names
    for (const auto& map : maps) {
        EXPECT_FALSE(map.name.empty());
        // ECU labels typically contain alphanumeric + underscores
        EXPECT_TRUE(map.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == std::string::npos);
    }
}
