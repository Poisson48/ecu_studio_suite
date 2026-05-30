#include "ecu/RomPatcher.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace ecu {

namespace {

constexpr int16_t kSwordMax = 32767;
constexpr int16_t kSwordMin = -32768;

bool boundsOk(std::span<const uint8_t> rom, std::size_t off, std::size_t len) {
    return off + len <= rom.size();
}

struct MapDims {
    int         nx;
    int         ny;
    std::size_t xAxisOff;
    std::size_t yAxisOff;
    std::size_t dataOff;
};

std::expected<MapDims, std::string>
readMapDimensions(std::span<const uint8_t> rom, std::size_t address) {
    if (!boundsOk(rom, address, 4))
        return std::unexpected(
            std::format("Address 0x{:X} out of ROM bounds", address));

    const int nx = readSwordBE(rom, address);
    const int ny = readSwordBE(rom, address + 2);

    if (nx <= 0 || ny <= 0 || nx > 64 || ny > 64)
        return std::unexpected(
            std::format("Invalid map dimensions nx={} ny={} at 0x{:X}", nx, ny, address));

    const std::size_t xAxisOff = address + 4;
    const std::size_t yAxisOff = xAxisOff + static_cast<std::size_t>(nx) * 2;
    const std::size_t dataOff  = yAxisOff + static_cast<std::size_t>(ny) * 2;

    const std::size_t required = dataOff + static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) * 2;
    if (required > rom.size())
        return std::unexpected(
            std::format("Map at 0x{:X} extends past ROM end (need {} bytes)", address, required));

    return MapDims{nx, ny, xAxisOff, yAxisOff, dataOff};
}

} // namespace

int16_t readSwordBE(std::span<const uint8_t> rom, std::size_t off) {
    const uint16_t u = (static_cast<uint16_t>(rom[off]) << 8) | rom[off + 1];
    return static_cast<int16_t>(u);
}

void writeSwordBE(std::span<uint8_t> rom, std::size_t off, double value) {
    const double clamped = std::clamp(std::round(value),
                                      static_cast<double>(kSwordMin),
                                      static_cast<double>(kSwordMax));
    const auto   s       = static_cast<int16_t>(clamped);
    const auto   u       = static_cast<uint16_t>(s);
    rom[off]     = static_cast<uint8_t>((u >> 8) & 0xFF);
    rom[off + 1] = static_cast<uint8_t>(u & 0xFF);
}

std::expected<MapData, std::string>
readMapData(std::span<const uint8_t> rom, std::size_t address) {
    auto dimsResult = readMapDimensions(rom, address);
    if (!dimsResult)
        return std::unexpected(dimsResult.error());

    const auto& d = *dimsResult;

    MapData out;
    out.nx      = d.nx;
    out.ny      = d.ny;
    out.dataOff = d.dataOff;

    out.xAxis.resize(static_cast<std::size_t>(d.nx));
    for (int i = 0; i < d.nx; ++i)
        out.xAxis[static_cast<std::size_t>(i)] =
            readSwordBE(rom, d.xAxisOff + static_cast<std::size_t>(i) * 2);

    out.yAxis.resize(static_cast<std::size_t>(d.ny));
    for (int i = 0; i < d.ny; ++i)
        out.yAxis[static_cast<std::size_t>(i)] =
            readSwordBE(rom, d.yAxisOff + static_cast<std::size_t>(i) * 2);

    const std::size_t cellCount = static_cast<std::size_t>(d.nx) * static_cast<std::size_t>(d.ny);
    out.data.resize(cellCount);
    for (std::size_t i = 0; i < cellCount; ++i)
        out.data[i] = readSwordBE(rom, d.dataOff + i * 2);

    return out;
}

std::expected<std::vector<ChangedCell>, std::string>
applyPctToMap(std::span<uint8_t> rom, std::size_t address, double pct,
              ApplyPctOptions opts) {
    auto mapResult = readMapData(rom, address);
    if (!mapResult)
        return std::unexpected(mapResult.error());

    const auto& map    = *mapResult;
    const double factor = 1.0 + pct / 100.0;

    std::vector<ChangedCell> changed;

    for (std::size_t i = 0; i < map.data.size(); ++i) {
        const int16_t raw = map.data[i];
        if (opts.onlyPositive && raw <= 0)
            continue;

        const double scaled =
            std::clamp(std::round(static_cast<double>(raw) * factor),
                       static_cast<double>(opts.rawMin),
                       static_cast<double>(opts.rawMax));
        const auto newRaw = static_cast<int16_t>(scaled);

        if (newRaw != raw) {
            const std::size_t off = map.dataOff + i * 2;
            writeSwordBE(rom, off, static_cast<double>(newRaw));
            changed.push_back({off, raw, newRaw});
        }
    }

    return changed;
}

std::expected<int16_t, std::string>
readValue(std::span<const uint8_t> rom, std::size_t address) {
    if (!boundsOk(rom, address, 2))
        return std::unexpected(
            std::format("Address 0x{:X} out of ROM bounds", address));
    return readSwordBE(rom, address);
}

std::expected<void, std::string>
writeValue(std::span<uint8_t> rom, std::size_t address, double rawValue) {
    if (!boundsOk(rom, address, 2))
        return std::unexpected(
            std::format("Address 0x{:X} out of ROM bounds", address));
    writeSwordBE(rom, address, rawValue);
    return {};
}

} // namespace ecu
