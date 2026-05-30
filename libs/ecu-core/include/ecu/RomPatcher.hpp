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

struct ApplyPctOptions {
    bool    onlyPositive = true;
    int16_t rawMin       = -32768;
    int16_t rawMax       = 32767;
};

int16_t readSwordBE(std::span<const uint8_t> rom, std::size_t off);

void writeSwordBE(std::span<uint8_t> rom, std::size_t off, double value);

std::expected<MapData, std::string>
readMapData(std::span<const uint8_t> rom, std::size_t address);

std::expected<std::vector<ChangedCell>, std::string>
applyPctToMap(std::span<uint8_t> rom, std::size_t address, double pct,
              ApplyPctOptions opts = {});

std::expected<int16_t, std::string>
readValue(std::span<const uint8_t> rom, std::size_t address);

std::expected<void, std::string>
writeValue(std::span<uint8_t> rom, std::size_t address, double rawValue);

} // namespace ecu
