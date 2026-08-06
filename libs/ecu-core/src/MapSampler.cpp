#include "ecu/MapSampler.hpp"

#include <algorithm>
#include <cmath>

namespace ecu {

double axisToPhys(int16_t raw, const AxisScale& s) {
    return static_cast<double>(raw) * s.factor + s.offset;
}

double cellToPhys(int16_t raw, const AxisScale& s) {
    return static_cast<double>(raw) * s.factor + s.offset;
}

bool findAxisBracket(const std::vector<int16_t>& axisRaw,
                     const AxisScale& scale,
                     double targetPhys,
                     int& i0,
                     int& i1,
                     double& t) {
    if (axisRaw.size() < 2) return false;

    const int n = static_cast<int>(axisRaw.size());
    std::vector<double> phys(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        phys[static_cast<std::size_t>(i)] = axisToPhys(axisRaw[static_cast<std::size_t>(i)], scale);

    if (targetPhys <= phys.front()) {
        i0 = 0; i1 = 0; t = 0.0;
        return true;
    }
    if (targetPhys >= phys.back()) {
        i0 = n - 1; i1 = n - 1; t = 0.0;
        return true;
    }

    for (int i = 0; i < n - 1; ++i) {
        const double lo = phys[static_cast<std::size_t>(i)];
        const double hi = phys[static_cast<std::size_t>(i + 1)];
        if (targetPhys >= lo && targetPhys <= hi) {
            i0 = i;
            i1 = i + 1;
            const double span = hi - lo;
            t = span > 0.0 ? (targetPhys - lo) / span : 0.0;
            return true;
        }
    }
    i0 = n - 1; i1 = n - 1; t = 0.0;
    return true;
}

std::optional<MapSampleResult>
sampleMapBilinear(const MapData& map,
                  const AxisScale& xScale,
                  const AxisScale& yScale,
                  const AxisScale& dataScale,
                  double xPhys,
                  double yPhys) {
    if (map.nx < 1 || map.ny < 1) return std::nullopt;

    int ix0 = 0, ix1 = 0, iy0 = 0, iy1 = 0;
    double tx = 0.0, ty = 0.0;
    if (!findAxisBracket(map.xAxis, xScale, xPhys, ix0, ix1, tx)) return std::nullopt;
    if (!findAxisBracket(map.yAxis, yScale, yPhys, iy0, iy1, ty)) return std::nullopt;

    auto at = [&](int ix, int iy) -> double {
        const std::size_t idx = static_cast<std::size_t>(iy) * static_cast<std::size_t>(map.nx)
                              + static_cast<std::size_t>(ix);
        if (idx >= map.data.size()) return 0.0;
        return cellToPhys(map.data[idx], dataScale);
    };

    const double v00 = at(ix0, iy0);
    const double v10 = at(ix1, iy0);
    const double v01 = at(ix0, iy1);
    const double v11 = at(ix1, iy1);

    const double v0 = v00 + (v10 - v00) * tx;
    const double v1 = v01 + (v11 - v01) * tx;
    const double v  = v0 + (v1 - v0) * ty;

    MapSampleResult r;
    r.value  = v;
    r.ix0    = ix0;
    r.iy0    = iy0;
    r.xPhys  = xPhys;
    r.yPhys  = yPhys;
    return r;
}

} // namespace ecu
