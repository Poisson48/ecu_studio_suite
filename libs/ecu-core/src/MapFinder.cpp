#include "ecu/MapFinder.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace ecu {

namespace {

constexpr int kMinDataRange  = 5;
constexpr int kMinAxisSpan   = 10;

uint16_t readU16BE(std::span<const uint8_t> buf, std::size_t off) {
    return static_cast<uint16_t>((static_cast<uint16_t>(buf[off]) << 8) | buf[off + 1]);
}

int16_t readS16BE(std::span<const uint8_t> buf, std::size_t off) {
    return static_cast<int16_t>(readU16BE(buf, off));
}

// Returns +1 (strictly increasing), -1 (strictly decreasing), or 0 (neither).
int checkMonotonic(std::span<const int16_t> values) {
    if (values.size() < 2) return 0;
    bool inc = true, dec = true;
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (values[i] <= values[i - 1]) inc = false;
        if (values[i] >= values[i - 1]) dec = false;
        if (!inc && !dec) return 0;
    }
    return inc ? 1 : -1;
}

std::size_t computeBlockSize(int nx, int ny) {
    return 4
        + static_cast<std::size_t>(nx) * 2
        + static_cast<std::size_t>(ny) * 2
        + static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) * 2;
}

struct ScoreResult {
    int16_t dataMin;
    int16_t dataMax;
    double  smoothness;
    double  sizePref;
    double  variance;
    double  score;
};

std::optional<ScoreResult> scoreCandidate(std::span<const uint8_t> buf,
                                          std::size_t off, int nx, int ny,
                                          int minDataRange) {
    const std::size_t dataOff =
        off + 4
        + static_cast<std::size_t>(nx) * 2
        + static_cast<std::size_t>(ny) * 2;

    int16_t dmin = std::numeric_limits<int16_t>::max();
    int16_t dmax = std::numeric_limits<int16_t>::min();
    const std::size_t count = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
    for (std::size_t i = 0; i < count; ++i) {
        const int16_t v = readS16BE(buf, dataOff + i * 2);
        if (v < dmin) dmin = v;
        if (v > dmax) dmax = v;
    }
    const int range = static_cast<int>(dmax) - static_cast<int>(dmin);
    if (range < minDataRange) return std::nullopt;

    double totalDiff = 0.0;
    std::size_t diffCount = 0;

    for (int yi = 0; yi < ny; ++yi) {
        for (int xi = 0; xi < nx - 1; ++xi) {
            const int16_t a = readS16BE(buf, dataOff + (static_cast<std::size_t>(yi * nx + xi)) * 2);
            const int16_t b = readS16BE(buf, dataOff + (static_cast<std::size_t>(yi * nx + xi + 1)) * 2);
            totalDiff += std::abs(static_cast<int>(a) - static_cast<int>(b));
            ++diffCount;
        }
    }
    for (int xi = 0; xi < nx; ++xi) {
        for (int yi = 0; yi < ny - 1; ++yi) {
            const int16_t a = readS16BE(buf, dataOff + (static_cast<std::size_t>(yi * nx + xi)) * 2);
            const int16_t b = readS16BE(buf, dataOff + (static_cast<std::size_t>((yi + 1) * nx + xi)) * 2);
            totalDiff += std::abs(static_cast<int>(a) - static_cast<int>(b));
            ++diffCount;
        }
    }

    const double avgDiff    = totalDiff / static_cast<double>(std::max(std::size_t{1}, diffCount));
    const double smoothness = std::max(0.0, std::min(1.0, 1.0 - avgDiff / static_cast<double>(range)));

    // Peaks at nx+ny == 32 (i.e. a 16×16 map), tapers off symmetrically.
    const int    sizeSum  = nx + ny;
    const double sizePref = 1.0 - std::min(1.0, std::abs(sizeSum - 32) / 40.0);

    const double variance = std::min(1.0, static_cast<double>(range) / 1000.0);

    const double score = smoothness * 0.55 + sizePref * 0.2 + variance * 0.25;

    return ScoreResult{dmin, dmax, smoothness, sizePref, variance, score};
}

// Round a double to 3 decimal places, mirroring JS toFixed(3).
double round3(double v) {
    return std::round(v * 1000.0) / 1000.0;
}

} // namespace

std::vector<MapCandidate> findMaps(std::span<const uint8_t> buf,
                                   FindMapsOptions           opts) {
    const std::size_t startOffset =
        opts.startOffset.value_or(0);
    const std::size_t endOffset =
        std::min(opts.endOffset.value_or(buf.size()), buf.size());
    const int    minN       = opts.minN;
    const int    maxN       = opts.maxN;
    const int    step       = std::max(2, opts.step);
    const int    minAxisSpan = opts.minAxisSpan;
    const int    minDataRange = opts.minDataRange;
    const int    overlapGap = opts.overlapGap;

    std::vector<MapCandidate> raw;

    for (std::size_t off = startOffset;
         off + 8 < endOffset;
         off += static_cast<std::size_t>(step))
    {
        const int nx = static_cast<int>(readU16BE(buf, off));
        if (nx < minN || nx > maxN) continue;
        const int ny = static_cast<int>(readU16BE(buf, off + 2));
        if (ny < minN || ny > maxN) continue;

        const std::size_t blockSize = computeBlockSize(nx, ny);
        if (off + blockSize > endOffset) continue;

        const std::size_t axisXStart = off + 4;
        std::vector<int16_t> axisX(static_cast<std::size_t>(nx));
        for (int i = 0; i < nx; ++i)
            axisX[static_cast<std::size_t>(i)] =
                readS16BE(buf, axisXStart + static_cast<std::size_t>(i) * 2);

        const int xDir = checkMonotonic(axisX);
        if (!xDir) continue;
        const int xSpan = std::abs(static_cast<int>(axisX[static_cast<std::size_t>(nx - 1)])
                                 - static_cast<int>(axisX[0]));
        if (xSpan < minAxisSpan) continue;

        const std::size_t axisYStart = axisXStart + static_cast<std::size_t>(nx) * 2;
        std::vector<int16_t> axisY(static_cast<std::size_t>(ny));
        for (int i = 0; i < ny; ++i)
            axisY[static_cast<std::size_t>(i)] =
                readS16BE(buf, axisYStart + static_cast<std::size_t>(i) * 2);

        const int yDir = checkMonotonic(axisY);
        if (!yDir) continue;
        const int ySpan = std::abs(static_cast<int>(axisY[static_cast<std::size_t>(ny - 1)])
                                 - static_cast<int>(axisY[0]));
        if (ySpan < minAxisSpan) continue;

        const auto scored = scoreCandidate(buf, off, nx, ny, minDataRange);
        if (!scored) continue;

        const int16_t xMin = std::min(axisX[0], axisX[static_cast<std::size_t>(nx - 1)]);
        const int16_t xMax = std::max(axisX[0], axisX[static_cast<std::size_t>(nx - 1)]);
        const int16_t yMin = std::min(axisY[0], axisY[static_cast<std::size_t>(ny - 1)]);
        const int16_t yMax = std::max(axisY[0], axisY[static_cast<std::size_t>(ny - 1)]);

        raw.push_back(MapCandidate{
            .address   = off,
            .nx        = nx,
            .ny        = ny,
            .blockSize = blockSize,
            .axisX     = { xMin, xMax, xDir },
            .axisY     = { yMin, yMax, yDir },
            .data      = { scored->dataMin, scored->dataMax },
            .smoothness = round3(scored->smoothness),
            .score      = round3(scored->score),
        });
    }

    // Sort by score descending, then address ascending (mirrors JS tie-break).
    std::ranges::sort(raw, [](const MapCandidate& a, const MapCandidate& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.address < b.address;
    });

    // Deduplicate overlapping candidates — keep the highest-scoring one.
    std::vector<MapCandidate> kept;
    kept.reserve(std::min(raw.size(), opts.limit));

    for (const auto& r : raw) {
        const std::size_t rEnd = r.address + r.blockSize;
        bool overlaps = false;
        for (const auto& k : kept) {
            const std::size_t kEnd = k.address + k.blockSize;
            const bool separated =
                rEnd + static_cast<std::size_t>(overlapGap) <= k.address ||
                r.address >= kEnd + static_cast<std::size_t>(overlapGap);
            if (!separated) { overlaps = true; break; }
        }
        if (!overlaps) kept.push_back(r);
        if (kept.size() >= opts.limit) break;
    }

    return kept;
}

} // namespace ecu
