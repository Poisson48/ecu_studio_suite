#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ecu {

enum class CharType { VALUE, VAL_BLK, CURVE, MAP, OTHER };

struct AxisDef {
    int         maxAxisPoints = 0;
    std::size_t byteSize      = 2;
};

// Lightweight, diff-oriented characteristic record used by the map differ and
// report generator. Distinct from ecu::Characteristic (A2lParser.hpp), which is
// the full A2L record. Kept separate so the two headers can coexist in one TU.
struct DiffCharacteristic {
    std::string           name;
    CharType              type        = CharType::OTHER;
    uint32_t              address     = 0;
    bool                  hasAddress  = false;
    std::size_t           byteSize    = 2;
    std::optional<AxisDef> xAxis;
    std::optional<AxisDef> yAxis;
    std::string           unit;
    std::string           description;
};

struct SampleChange {
    std::size_t offset;
    int16_t     before;
    int16_t     after;
};

struct AvgChange {
    double      avgRatio;
    std::size_t countedCells;
};

struct DiffInterval {
    std::size_t start;
    std::size_t end;
};

struct MapDiffResult {
    std::string              name;
    CharType                 type;
    uint32_t                 address;
    std::size_t              size;
    std::string              unit;
    std::string              description;
    std::size_t              cellsChanged;
    std::size_t              totalCells;
    double                   tightness;
    std::optional<SampleChange> sample;
    std::optional<AvgChange>    avg;
};

struct MapsChangedResult {
    std::vector<DiffInterval>   intervals;
    std::vector<MapDiffResult>  maps;
};

std::size_t estimateRegionSize(const DiffCharacteristic& c);

std::vector<DiffInterval> diffIntervals(std::span<const uint8_t> a,
                                        std::span<const uint8_t> b);

MapsChangedResult mapsChanged(std::span<const uint8_t>         bufA,
                              std::span<const uint8_t>         bufB,
                              std::span<const DiffCharacteristic>  characteristics);

} // namespace ecu
