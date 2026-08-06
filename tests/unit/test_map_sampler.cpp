#include <gtest/gtest.h>
#include "ecu/MapSampler.hpp"

using namespace ecu;

TEST(MapSampler, BilinearCenter) {
    MapData map;
    map.nx = 2;
    map.ny = 2;
    map.xAxis = { 1000, 2000 };
    map.yAxis = { 0, 100 };
    map.data  = { 1000, 1100, 1200, 1300 };

    const AxisScale xS{ 1.0, 0.0 };
    const AxisScale yS{ 1.0, 0.0 };
    const AxisScale dS{ 1.0, 0.0 };

    auto r = sampleMapBilinear(map, xS, yS, dS, 1500.0, 50.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->value, 1150.0);
}

TEST(MapSampler, ClampsOutsideRange) {
    MapData map;
    map.nx = 2;
    map.ny = 2;
    map.xAxis = { 1000, 2000 };
    map.yAxis = { 0, 100 };
    map.data  = { 1000, 1100, 1200, 1300 };

    auto r = sampleMapBilinear(map, {}, {}, {}, 500.0, 50.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->value, 1100.0);
}
