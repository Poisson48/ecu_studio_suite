#pragma once
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace ecu {

struct ChangedCell {
    std::size_t offset;
    int16_t     oldValue;
    int16_t     newValue;
};

struct MapData {
    int                  nx;
    int                  ny;
    std::vector<int16_t> xAxis;
    std::vector<int16_t> yAxis;
    std::vector<int16_t> data;
    std::size_t          dataOff;
};

// CURVE (record layout Kl_Xs16_Ws16): 2-byte header (nx UWORD_BE) followed by
// the X axis then the values, both nx SWORD_BE entries. Unlike MapData there is
// no ny field, so reading a curve with readMapData() consumes xAxis[0] as ny.
struct CurveData {
    int                  nx;
    std::vector<int16_t> xAxis;
    std::vector<int16_t> data;
    std::size_t          dataOff;
};

struct ApplyPctOptions {
    bool    onlyPositive = true;
    int16_t rawMin       = -32768;
    int16_t rawMax       = 32767;
};

int16_t readSwordBE(std::span<const uint8_t> rom, std::size_t off);

void writeSwordBE(std::span<uint8_t> rom, std::size_t off, double value);

std::expected<MapData, std::string>
readMapData(std::span<const uint8_t> rom, std::size_t address);

std::expected<CurveData, std::string>
readCurveData(std::span<const uint8_t> rom, std::size_t address);

std::expected<std::vector<ChangedCell>, std::string>
applyPctToMap(std::span<uint8_t> rom, std::size_t address, double pct,
              ApplyPctOptions opts = {});

std::expected<int16_t, std::string>
readValue(std::span<const uint8_t> rom, std::size_t address);

std::expected<void, std::string>
writeValue(std::span<uint8_t> rom, std::size_t address, double rawValue);

} // namespace ecu
