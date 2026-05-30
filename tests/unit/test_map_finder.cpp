#include <gtest/gtest.h>
#include "ecu/MapFinder.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

using namespace ecu;

namespace {

void writeBE(std::vector<uint8_t>& buf, std::size_t off, int16_t value) {
    const auto u = static_cast<uint16_t>(value);
    buf[off]     = static_cast<uint8_t>((u >> 8) & 0xFF);
    buf[off + 1] = static_cast<uint8_t>(u & 0xFF);
}

// Embed a clean nx*ny map at `addr`: monotonic axes (wide span) and smooth,
// gently increasing data, exactly the shape findMaps is tuned to detect.
void embedMap(std::vector<uint8_t>& buf, std::size_t addr, int nx, int ny) {
    writeBE(buf, addr, static_cast<int16_t>(nx));
    writeBE(buf, addr + 2, static_cast<int16_t>(ny));

    std::size_t off = addr + 4;
    for (int i = 0; i < nx; ++i) { // strictly increasing X axis
        writeBE(buf, off, static_cast<int16_t>(100 + i * 50));
        off += 2;
    }
    for (int j = 0; j < ny; ++j) { // strictly increasing Y axis
        writeBE(buf, off, static_cast<int16_t>(500 + j * 60));
        off += 2;
    }
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            // Smooth bilinear ramp — small neighbour deltas, decent total range.
            writeBE(buf, off, static_cast<int16_t>(200 + i * 8 + j * 8));
            off += 2;
        }
    }
}

} // namespace

TEST(MapFinder, LocatesEmbeddedMap) {
    const std::size_t addr = 256;
    const int nx = 16, ny = 16;

    // Buffer of zeros (noise floor) large enough to hold the map plus slack.
    std::vector<uint8_t> buf(4096, 0x00);
    embedMap(buf, addr, nx, ny);

    auto cands = findMaps(std::span<const uint8_t>(buf));
    ASSERT_FALSE(cands.empty());

    auto it = std::find_if(cands.begin(), cands.end(),
                           [&](const MapCandidate& c) { return c.address == addr; });
    ASSERT_NE(it, cands.end()) << "no candidate found at the embedded address";

    EXPECT_EQ(it->nx, nx);
    EXPECT_EQ(it->ny, ny);
    EXPECT_EQ(it->axisX.dir, 1);
    EXPECT_EQ(it->axisY.dir, 1);
    EXPECT_GT(it->score, 0.0);

    // A clean 16x16 map should be the top-ranked candidate.
    EXPECT_EQ(cands.front().address, addr);
}

TEST(MapFinder, FindsNothingInPureNoiseZeros) {
    // All-zero buffer: axes have zero span, so nothing qualifies.
    std::vector<uint8_t> buf(2048, 0x00);
    auto cands = findMaps(std::span<const uint8_t>(buf));
    EXPECT_TRUE(cands.empty());
}
