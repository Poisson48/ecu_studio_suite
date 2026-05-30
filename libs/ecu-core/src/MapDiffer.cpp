#include "ecu/MapDiffer.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace ecu {

namespace {

int16_t toSigned(uint16_t u) {
    return static_cast<int16_t>(u);
}

uint16_t readU16BE(std::span<const uint8_t> buf, std::size_t i) {
    return static_cast<uint16_t>((static_cast<uint16_t>(buf[i]) << 8) | buf[i + 1]);
}

std::optional<SampleChange>
sampleChange(std::span<const uint8_t> a, std::span<const uint8_t> b,
             std::size_t addr, std::size_t size) {
    for (std::size_t off = 0; off + 1 < size; off += 2) {
        const std::size_t i = addr + off;
        if (i + 1 >= a.size() || i + 1 >= b.size()) break;
        const int16_t ra = toSigned(readU16BE(a, i));
        const int16_t rb = toSigned(readU16BE(b, i));
        if (ra != rb)
            return SampleChange{off, ra, rb};
    }
    return std::nullopt;
}

std::optional<AvgChange>
avgChange(std::span<const uint8_t> a, std::span<const uint8_t> b,
          std::size_t addr, std::size_t size) {
    double      sum = 0.0;
    std::size_t n   = 0;
    for (std::size_t off = 0; off + 1 < size; off += 2) {
        const std::size_t i = addr + off;
        if (i + 1 >= a.size() || i + 1 >= b.size()) break;
        const int16_t ra = toSigned(readU16BE(a, i));
        const int16_t rb = toSigned(readU16BE(b, i));
        if (ra == rb) continue;
        if (ra == 0) continue;
        // Divide by signed ra, not abs(ra): preserves sign of pct for negative
        // cells. Using abs(ra) would invert the contribution for negative cells
        // and produce a wrong average.
        sum += static_cast<double>(rb - ra) / static_cast<double>(ra);
        ++n;
    }
    if (n == 0) return std::nullopt;
    return AvgChange{sum / static_cast<double>(n), n};
}

std::size_t countDiffCells(std::span<const uint8_t> a, std::span<const uint8_t> b,
                           std::size_t addr, std::size_t size) {
    std::size_t n = 0;
    for (std::size_t off = 0; off + 1 < size; off += 2) {
        const std::size_t i = addr + off;
        if (i + 1 >= a.size() || i + 1 >= b.size()) break;
        if (a[i] != b[i] || a[i + 1] != b[i + 1]) ++n;
    }
    return n;
}

int typeWeight(CharType t) {
    switch (t) {
        case CharType::MAP:     return 3;
        case CharType::CURVE:   return 2;
        case CharType::VAL_BLK: return 2;
        case CharType::VALUE:   return 1;
        default:                return 0;
    }
}

} // namespace

std::size_t estimateRegionSize(const DiffCharacteristic& c) {
    const int  xPts     = c.xAxis ? c.xAxis->maxAxisPoints : 0;
    const int  yPts     = c.yAxis ? c.yAxis->maxAxisPoints : 0;
    const auto valSize  = c.byteSize;
    const auto xAxisSz  = c.xAxis ? c.xAxis->byteSize : std::size_t{2};
    const auto yAxisSz  = c.yAxis ? c.yAxis->byteSize : std::size_t{2};

    switch (c.type) {
        case CharType::VALUE:
            return valSize;
        case CharType::VAL_BLK: {
            const std::size_t cells = xPts > 0 ? static_cast<std::size_t>(xPts) : 1;
            const std::size_t sz    = cells * valSize;
            return sz > 0 ? sz : valSize;
        }
        case CharType::CURVE:
            return 2
                 + static_cast<std::size_t>(xPts) * xAxisSz
                 + static_cast<std::size_t>(xPts) * valSize;
        case CharType::MAP:
            return 4
                 + static_cast<std::size_t>(xPts) * xAxisSz
                 + static_cast<std::size_t>(yPts) * yAxisSz
                 + static_cast<std::size_t>(xPts) * static_cast<std::size_t>(yPts) * valSize;
        default:
            return valSize;
    }
}

std::vector<DiffInterval> diffIntervals(std::span<const uint8_t> a,
                                        std::span<const uint8_t> b) {
    std::vector<DiffInterval> intervals;
    const std::size_t minLen = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < minLen) {
        if (a[i] != b[i]) {
            const std::size_t start = i;
            while (i < minLen && a[i] != b[i]) ++i;
            intervals.push_back({start, i});
        } else {
            ++i;
        }
    }
    if (a.size() != b.size())
        intervals.push_back({minLen, std::max(a.size(), b.size())});
    return intervals;
}

MapsChangedResult mapsChanged(std::span<const uint8_t>        bufA,
                              std::span<const uint8_t>        bufB,
                              std::span<const DiffCharacteristic> characteristics) {
    auto intervals = diffIntervals(bufA, bufB);
    if (intervals.empty())
        return MapsChangedResult{};

    std::vector<MapDiffResult>   results;
    std::unordered_set<std::string> seen;

    for (const auto& c : characteristics) {
        if (!c.hasAddress) continue;
        const std::size_t size = estimateRegionSize(c);
        if (size == 0) continue;

        const std::size_t cStart = c.address;
        const std::size_t cEnd   = c.address + size;

        bool overlap = false;
        for (const auto& iv : intervals) {
            if (iv.end   <= cStart) continue;
            if (iv.start >= cEnd)   break;
            overlap = true;
            break;
        }
        if (!overlap) continue;
        if (!seen.insert(c.name).second) continue;

        const std::size_t cellsChanged = countDiffCells(bufA, bufB, c.address, size);
        const std::size_t totalCells   = std::max(std::size_t{1}, size / 2);
        const double      tightness    = static_cast<double>(cellsChanged)
                                       / static_cast<double>(totalCells);

        std::string desc = c.description;
        if (desc.size() > 120) desc.resize(120);

        results.push_back(MapDiffResult{
            .name         = c.name,
            .type         = c.type,
            .address      = c.address,
            .size         = size,
            .unit         = c.unit,
            .description  = std::move(desc),
            .cellsChanged = cellsChanged,
            .totalCells   = totalCells,
            .tightness    = tightness,
            .sample       = sampleChange(bufA, bufB, c.address, size),
            .avg          = avgChange(bufA, bufB, c.address, size),
        });
    }

    std::ranges::sort(results, [](const MapDiffResult& x, const MapDiffResult& y) {
        if (std::abs(y.tightness - x.tightness) > 0.02)
            return y.tightness < x.tightness;
        if (y.cellsChanged != x.cellsChanged)
            return y.cellsChanged < x.cellsChanged;
        return typeWeight(y.type) < typeWeight(x.type);
    });

    return MapsChangedResult{std::move(intervals), std::move(results)};
}

} // namespace ecu
