#include <gtest/gtest.h>
#include "ecu/ChecksumEngine.hpp"

#include <cstdint>
#include <span>
#include <vector>

using namespace ecu;

namespace {

// Known-answer vector for CRC-16/ARC: CRC("123456789") == 0xBB3D.
TEST(Crc16Arc, KnownAnswerVector) {
    const std::vector<uint8_t> msg{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(crc16Arc(msg), 0xBB3D);
}

TEST(Crc16Arc, EmptyIsZeroInit) {
    EXPECT_EQ(crc16Arc(std::span<const uint8_t>{}), 0x0000);
}

// Build a 32 KB image, fill the checksum window with a pattern, then compute and
// store the correct big-endian CRC. verifyMpps must report it valid.
TEST(ChecksumEngine, Verify32kRoundTrip) {
    std::vector<uint8_t> rom(0x8000, 0x00);
    for (std::size_t i = 0x30; i < 0x7FEA; ++i)
        rom[i] = static_cast<uint8_t>((i * 31 + 7) & 0xFF);

    auto region = mppsRegionForSize(rom.size());
    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region->start, 0x0030u);
    EXPECT_EQ(region->end, 0x7FEAu);
    EXPECT_EQ(region->checksumOff, 0x7FEAu);

    const uint16_t crc =
        crc16Arc(std::span<const uint8_t>(rom).subspan(0x30, 0x7FEA - 0x30));
    ChecksumEngine::writeStoredBE(rom, 0x7FEA, crc);

    auto res = ChecksumEngine::verifyMpps(rom);
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(res->valid);
    EXPECT_EQ(res->computed, crc);
    EXPECT_EQ(res->stored, crc);
}

// A corrupted stored checksum must verify false, then correctMpps fixes it.
TEST(ChecksumEngine, Correct32kFixesInPlace) {
    std::vector<uint8_t> rom(0x8000, 0xA5);
    // Deliberately wrong stored checksum.
    ChecksumEngine::writeStoredBE(rom, 0x7FEA, 0xDEAD);

    auto before = ChecksumEngine::verifyMpps(rom);
    ASSERT_TRUE(before.has_value());
    EXPECT_FALSE(before->valid);

    auto fixed = ChecksumEngine::correctMpps(rom);
    ASSERT_TRUE(fixed.has_value());
    EXPECT_EQ(*fixed, 1); // one checksum corrected

    auto after = ChecksumEngine::verifyMpps(rom);
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(after->valid);

    // Correcting an already-valid image reports zero fixes.
    EXPECT_EQ(ChecksumEngine::correctMpps(rom).value(), 0);
}

// 64 KB layout uses window [0x8041, 0xFFFA) and stored @ 0xFFFA.
TEST(ChecksumEngine, Region64kLayout) {
    auto region = mppsRegionForSize(0x10000);
    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region->start, 0x8041u);
    EXPECT_EQ(region->end, 0xFFFAu);
    EXPECT_EQ(region->checksumOff, 0xFFFAu);

    std::vector<uint8_t> rom(0x10000, 0x3C);
    const uint16_t crc =
        crc16Arc(std::span<const uint8_t>(rom).subspan(0x8041, 0xFFFA - 0x8041));
    ChecksumEngine::writeStoredBE(rom, 0xFFFA, crc);
    auto res = ChecksumEngine::verifyMpps(rom);
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(res->valid);
}

// Unknown sizes are rejected (engine never guesses a layout).
TEST(ChecksumEngine, UnknownSizeRejected) {
    std::vector<uint8_t> rom(0x80000, 0x00); // 512 KB EDC16 — per-ECU, not impl.
    EXPECT_EQ(mppsLayoutForSize(rom.size()), EdcLayout::Unknown);
    EXPECT_FALSE(ChecksumEngine::verifyMpps(rom).has_value());
    EXPECT_FALSE(ChecksumEngine::correctMpps(rom).has_value());
}

TEST(ChecksumEngine, BigEndianAccessors) {
    std::vector<uint8_t> buf(4, 0x00);
    ChecksumEngine::writeStoredBE(buf, 1, 0x1234);
    EXPECT_EQ(buf[1], 0x12);
    EXPECT_EQ(buf[2], 0x34);
    EXPECT_EQ(ChecksumEngine::readStoredBE(buf, 1), 0x1234);
}

} // namespace
