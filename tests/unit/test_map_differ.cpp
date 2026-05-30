#include <gtest/gtest.h>
#include "ecu/MapDiffer.hpp"

#include <cstdint>
#include <span>
#include <vector>

using namespace ecu;

TEST(MapDiffer, DiffIntervalsFindsContiguousRuns) {
    std::vector<uint8_t> a(64, 0x00);
    std::vector<uint8_t> b = a;

    // Two separate changed ranges: [4,8) and [20,23).
    b[4] = 1; b[5] = 1; b[6] = 1; b[7] = 1;
    b[20] = 9; b[21] = 9; b[22] = 9;

    auto intervals = diffIntervals(std::span<const uint8_t>(a),
                                   std::span<const uint8_t>(b));
    ASSERT_EQ(intervals.size(), 2u);

    EXPECT_EQ(intervals[0].start, 4u);
    EXPECT_EQ(intervals[0].end, 8u);
    EXPECT_EQ(intervals[1].start, 20u);
    EXPECT_EQ(intervals[1].end, 23u);
}

TEST(MapDiffer, DiffIntervalsEmptyWhenIdentical) {
    std::vector<uint8_t> a(32, 0x55);
    std::vector<uint8_t> b = a;
    auto intervals = diffIntervals(std::span<const uint8_t>(a),
                                   std::span<const uint8_t>(b));
    EXPECT_TRUE(intervals.empty());
}

TEST(MapDiffer, DiffIntervalsTrailingSizeMismatch) {
    std::vector<uint8_t> a(16, 0x00);
    std::vector<uint8_t> b(20, 0x00); // 4 extra bytes, otherwise identical

    auto intervals = diffIntervals(std::span<const uint8_t>(a),
                                   std::span<const uint8_t>(b));
    ASSERT_EQ(intervals.size(), 1u);
    EXPECT_EQ(intervals[0].start, 16u);
    EXPECT_EQ(intervals[0].end, 20u);
}

TEST(MapDiffer, DiffIntervalsSingleByte) {
    std::vector<uint8_t> a(10, 0x00);
    std::vector<uint8_t> b = a;
    b[5] = 0xFF;

    auto intervals = diffIntervals(std::span<const uint8_t>(a),
                                   std::span<const uint8_t>(b));
    ASSERT_EQ(intervals.size(), 1u);
    EXPECT_EQ(intervals[0].start, 5u);
    EXPECT_EQ(intervals[0].end, 6u);
}
