#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ecu {

struct FindMapsOptions {
    int         minN        = 4;
    int         maxN        = 32;
    int         step        = 2;
    int         minAxisSpan = 10;
    int         minDataRange = 5;
    std::size_t limit       = 100;
    int         overlapGap  = 16;
    std::optional<std::size_t> startOffset;
    std::optional<std::size_t> endOffset;
};

struct AxisSummary {
    int16_t min;
    int16_t max;
    int     dir; // +1 increasing, -1 decreasing
};

struct DataSummary {
    int16_t min;
    int16_t max;
};

struct MapCandidate {
    std::size_t  address;
    int          nx;
    int          ny;
    std::size_t  blockSize;
    AxisSummary  axisX;
    AxisSummary  axisY;
    DataSummary  data;
    double       smoothness;
    double       score;
};

std::vector<MapCandidate> findMaps(std::span<const uint8_t> buf,
                                   FindMapsOptions           opts = {});

} // namespace ecu
