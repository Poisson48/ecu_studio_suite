#include <gtest/gtest.h>
#include "ecu/Obd2.hpp"

using namespace ecu::obd2;

TEST(Obd2, PidRequestFormat) {
    EXPECT_EQ(pidRequest(0x0C), QStringLiteral("010C"));
    EXPECT_EQ(pidRequest(0x02, 0x09), QStringLiteral("0902"));
}

TEST(Obd2, ParseRpmHeaderless) {
    auto r = parseResponse(QStringLiteral("41 0C 1A F8"), 0x01, 0x0C);
    ASSERT_TRUE(r.ok);
    ASSERT_GE(r.len, 2);
    auto v = interpret(0x0C, r.data.data(), r.len);
    ASSERT_TRUE(v.has_value());
    EXPECT_DOUBLE_EQ(*v, 1726.0);   // (256*0x1A + 0xF8)/4
}

TEST(Obd2, ParseRpmWithCanHeaderAndLength) {
    // header CAN 11 bits (7E8) + longueur ISO-TP (06) + données.
    auto r = parseResponse(QStringLiteral("7E8 06 41 0C 1A F8 00 00"), 0x01, 0x0C);
    ASSERT_TRUE(r.ok);
    auto v = interpret(0x0C, r.data.data(), r.len);
    ASSERT_TRUE(v.has_value());
    EXPECT_DOUBLE_EQ(*v, 1726.0);
}

TEST(Obd2, InterpretCommonPids) {
    std::uint8_t coolant[1] = { 0x5A };   // 90 - 40 = 50 °C
    EXPECT_DOUBLE_EQ(*interpret(0x05, coolant, 1), 50.0);
    std::uint8_t map[1] = { 0x64 };       // 100 kPa
    EXPECT_DOUBLE_EQ(*interpret(0x0B, map, 1), 100.0);
    std::uint8_t speed[1] = { 0x50 };     // 80 km/h
    EXPECT_DOUBLE_EQ(*interpret(0x0D, speed, 1), 80.0);
}

TEST(Obd2, ParseFailsOnNoData) {
    auto r = parseResponse(QStringLiteral("NO DATA"), 0x01, 0x0C);
    EXPECT_FALSE(r.ok);
}

TEST(Obd2, DecodeDtcs) {
    auto codes = decodeDtcs(QStringLiteral("43 01 33 02 47 00 00"));
    ASSERT_EQ(codes.size(), 2);
    EXPECT_EQ(codes[0], QStringLiteral("P0133"));
    EXPECT_EQ(codes[1], QStringLiteral("P0247"));
}

TEST(Obd2, DecodeVin) {
    // 49 02 01 + ASCII de "WP0ZZZ99ZTS392124"
    const QString resp = QStringLiteral(
        "49 02 01 57 50 30 5A 5A 5A 39 39 5A 54 53 33 39 32 31 32 34");
    EXPECT_EQ(decodeVin(resp), QStringLiteral("WP0ZZZ99ZTS392124"));
}
