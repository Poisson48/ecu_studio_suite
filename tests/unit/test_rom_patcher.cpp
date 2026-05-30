#include <gtest/gtest.h>
#include "ecu/RomPatcher.hpp"

#include <cstdint>
#include <span>
#include <vector>

using namespace ecu;

namespace {

// Append a big-endian SWORD to a byte vector.
void pushBE(std::vector<uint8_t>& v, int16_t value) {
    const auto u = static_cast<uint16_t>(value);
    v.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>(u & 0xFF));
}

// Build a synthetic ROM containing a single Kf_Xs16_Ys16_Ws16-style map at the
// returned address. Layout: [nx][ny][xAxis...][yAxis...][data row-major...].
std::vector<uint8_t> buildRom(std::size_t mapAddress, int nx, int ny,
                              const std::vector<int16_t>& xAxis,
                              const std::vector<int16_t>& yAxis,
                              const std::vector<int16_t>& data) {
    std::vector<uint8_t> rom(mapAddress, 0x00);
    pushBE(rom, static_cast<int16_t>(nx));
    pushBE(rom, static_cast<int16_t>(ny));
    for (auto x : xAxis) pushBE(rom, x);
    for (auto y : yAxis) pushBE(rom, y);
    for (auto d : data) pushBE(rom, d);
    // Trailing padding so out-of-map reads remain in bounds.
    rom.resize(rom.size() + 32, 0x00);
    return rom;
}

} // namespace

TEST(RomPatcher, ReadSwordBERoundTrip) {
    std::vector<uint8_t> buf(2, 0);
    std::span<uint8_t> w(buf);
    writeSwordBE(w, 0, 12345.0);
    EXPECT_EQ(readSwordBE(std::span<const uint8_t>(buf), 0), 12345);

    writeSwordBE(w, 0, -2000.0);
    EXPECT_EQ(readSwordBE(std::span<const uint8_t>(buf), 0), -2000);

    // Explicit byte order check: 0x3039 == 12345.
    writeSwordBE(w, 0, 12345.0);
    EXPECT_EQ(buf[0], 0x30);
    EXPECT_EQ(buf[1], 0x39);
}

TEST(RomPatcher, WriteSwordBEClampsAndRounds) {
    std::vector<uint8_t> buf(2, 0);
    std::span<uint8_t> w(buf);

    writeSwordBE(w, 0, 100000.0); // above SWORD max
    EXPECT_EQ(readSwordBE(std::span<const uint8_t>(buf), 0), 32767);

    writeSwordBE(w, 0, -100000.0); // below SWORD min
    EXPECT_EQ(readSwordBE(std::span<const uint8_t>(buf), 0), -32768);

    writeSwordBE(w, 0, 10.4); // rounds to nearest
    EXPECT_EQ(readSwordBE(std::span<const uint8_t>(buf), 0), 10);
    writeSwordBE(w, 0, 10.6);
    EXPECT_EQ(readSwordBE(std::span<const uint8_t>(buf), 0), 11);
}

TEST(RomPatcher, ReadMapDataReturnsDimsAxesAndData) {
    const std::size_t addr = 16;
    const int nx = 3, ny = 2;
    const std::vector<int16_t> xAxis = {100, 200, 300};
    const std::vector<int16_t> yAxis = {1000, 2000};
    const std::vector<int16_t> data  = {10, 20, 30, 40, 50, 60};

    auto rom = buildRom(addr, nx, ny, xAxis, yAxis, data);

    auto res = readMapData(std::span<const uint8_t>(rom), addr);
    ASSERT_TRUE(res.has_value()) << res.error();

    EXPECT_EQ(res->nx, nx);
    EXPECT_EQ(res->ny, ny);
    EXPECT_EQ(res->xAxis, xAxis);
    EXPECT_EQ(res->yAxis, yAxis);
    EXPECT_EQ(res->data, data);
    // dataOff = addr + 4 (dims) + nx*2 + ny*2.
    EXPECT_EQ(res->dataOff, addr + 4 + nx * 2 + ny * 2);
}

TEST(RomPatcher, ReadMapDataRejectsBadDimensions) {
    std::vector<uint8_t> rom(64, 0x00); // nx=ny=0
    auto res = readMapData(std::span<const uint8_t>(rom), 0);
    EXPECT_FALSE(res.has_value());
}

TEST(RomPatcher, ApplyPctScalesOnlyPositiveValues) {
    const std::size_t addr = 8;
    const int nx = 2, ny = 2;
    const std::vector<int16_t> xAxis = {0, 100};
    const std::vector<int16_t> yAxis = {0, 100};
    // Mix of positive, zero and negative cells.
    const std::vector<int16_t> data  = {100, -100, 0, 200};

    auto rom = buildRom(addr, nx, ny, xAxis, yAxis, data);

    // +10% — factor 1.1. Only positive cells should change.
    auto changed = applyPctToMap(std::span<uint8_t>(rom), addr, 10.0);
    ASSERT_TRUE(changed.has_value()) << changed.error();

    auto after = readMapData(std::span<const uint8_t>(rom), addr);
    ASSERT_TRUE(after.has_value());

    EXPECT_EQ(after->data[0], 110);  // 100 * 1.1
    EXPECT_EQ(after->data[1], -100); // negative untouched
    EXPECT_EQ(after->data[2], 0);    // zero untouched
    EXPECT_EQ(after->data[3], 220);  // 200 * 1.1

    // Only the two positive cells changed.
    EXPECT_EQ(changed->size(), 2u);
    for (const auto& c : *changed) {
        EXPECT_GT(c.oldValue, 0);
        EXPECT_NE(c.oldValue, c.newValue);
    }
}

TEST(RomPatcher, ApplyPctOnlyPositiveDisabledScalesNegatives) {
    const std::size_t addr = 8;
    const int nx = 2, ny = 1;
    const std::vector<int16_t> xAxis = {0, 100};
    const std::vector<int16_t> yAxis = {0};
    const std::vector<int16_t> data  = {-100, 200};

    auto rom = buildRom(addr, nx, ny, xAxis, yAxis, data);

    ApplyPctOptions opts;
    opts.onlyPositive = false;
    auto changed = applyPctToMap(std::span<uint8_t>(rom), addr, 10.0, opts);
    ASSERT_TRUE(changed.has_value());

    auto after = readMapData(std::span<const uint8_t>(rom), addr);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->data[0], -110); // -100 * 1.1
    EXPECT_EQ(after->data[1], 220);
}

TEST(RomPatcher, ReadWriteValue) {
    std::vector<uint8_t> rom(16, 0x00);

    auto wr = writeValue(std::span<uint8_t>(rom), 4, 4242.0);
    ASSERT_TRUE(wr.has_value());

    auto rd = readValue(std::span<const uint8_t>(rom), 4);
    ASSERT_TRUE(rd.has_value());
    EXPECT_EQ(*rd, 4242);

    // Out-of-bounds read/write reported as error.
    EXPECT_FALSE(readValue(std::span<const uint8_t>(rom), 16).has_value());
    EXPECT_FALSE(writeValue(std::span<uint8_t>(rom), 16, 1.0).has_value());
}
