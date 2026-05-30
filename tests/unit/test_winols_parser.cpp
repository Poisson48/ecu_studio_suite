#include <gtest/gtest.h>
#include "ecu/WinolsParser.hpp"

#include <QByteArray>
#include <QString>

using namespace ecu;

namespace {

// Build a single Intel HEX data record: ":" len addr type data checksum.
QByteArray hexRecord(uint16_t addr, uint8_t type, const QByteArray& data) {
    QByteArray rec;
    const uint8_t len = static_cast<uint8_t>(data.size());
    rec.append(static_cast<char>(len));
    rec.append(static_cast<char>((addr >> 8) & 0xFF));
    rec.append(static_cast<char>(addr & 0xFF));
    rec.append(static_cast<char>(type));
    rec.append(data);

    uint8_t sum = 0;
    for (char c : rec) sum += static_cast<uint8_t>(c);
    const uint8_t checksum = static_cast<uint8_t>((~sum) + 1); // two's complement
    rec.append(static_cast<char>(checksum));

    return ":" + rec.toHex().toUpper() + "\r\n";
}

} // namespace

TEST(WinolsParser, ParsesIntelHexByExtension) {
    // 8 known bytes at address 0, then EOF.
    QByteArray payload;
    for (uint8_t i = 0; i < 8; ++i)
        payload.append(static_cast<char>(0x10 + i)); // 0x10..0x17

    QByteArray hex;
    hex += hexRecord(0x0000, 0x00, payload);
    hex += hexRecord(0x0000, 0x01, QByteArray()); // EOF record

    WinolsParser parser;
    auto res = parser.parse(hex, "image.hex");
    ASSERT_TRUE(res.has_value()) << res.error().toStdString();

    EXPECT_EQ(res->rom.size(), 8);
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(static_cast<uint8_t>(res->rom.at(i)),
                  static_cast<uint8_t>(0x10 + i));

    // .hex suffix is rewritten to .bin.
    EXPECT_EQ(res->filename, QStringLiteral("image.bin"));
}

TEST(WinolsParser, IntelHexGapFilledWithFF) {
    // Two records leaving a 4-byte gap that must be filled with 0xFF.
    QByteArray a, b;
    a.append(static_cast<char>(0xAA));
    a.append(static_cast<char>(0xBB));
    b.append(static_cast<char>(0xCC));
    b.append(static_cast<char>(0xDD));

    QByteArray hex;
    hex += hexRecord(0x0000, 0x00, a);  // bytes 0,1
    hex += hexRecord(0x0006, 0x00, b);  // bytes 6,7
    hex += hexRecord(0x0000, 0x01, QByteArray());

    WinolsParser parser;
    auto res = parser.parse(hex, "gap.hex");
    ASSERT_TRUE(res.has_value()) << res.error().toStdString();

    ASSERT_EQ(res->rom.size(), 8);
    EXPECT_EQ(static_cast<uint8_t>(res->rom.at(0)), 0xAA);
    EXPECT_EQ(static_cast<uint8_t>(res->rom.at(1)), 0xBB);
    for (int i = 2; i < 6; ++i)
        EXPECT_EQ(static_cast<uint8_t>(res->rom.at(i)), 0xFF);
    EXPECT_EQ(static_cast<uint8_t>(res->rom.at(6)), 0xCC);
    EXPECT_EQ(static_cast<uint8_t>(res->rom.at(7)), 0xDD);
}

TEST(WinolsParser, RawBinaryPassthrough) {
    QByteArray bin;
    for (int i = 0; i < 32; ++i)
        bin.append(static_cast<char>(i));

    WinolsParser parser;
    auto res = parser.parse(bin, "dump.bin");
    ASSERT_TRUE(res.has_value()) << res.error().toStdString();

    EXPECT_EQ(res->rom, bin);
    EXPECT_EQ(res->filename, QStringLiteral("dump.bin"));
    EXPECT_TRUE(res->maps.isEmpty());
}

TEST(WinolsParser, RawBinaryGetsBinSuffix) {
    QByteArray bin("\x01\x02\x03\x04", 4);
    WinolsParser parser;
    auto res = parser.parse(bin, "noext");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->filename, QStringLiteral("noext.bin"));
    EXPECT_EQ(res->rom, bin);
}
