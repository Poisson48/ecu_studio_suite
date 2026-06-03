// OpenDamosRelocate.cpp — relocalisation des caractéristiques open_damos dans
// une ROM : lecture d'entiers BE, matcher d'axes flou (empreinte), scan par
// empreinte (findMapByFingerprint), relocalisation par ancre (findValueByAnchor)
// et orchestration complète (relocate). Extrait de OpenDamos.cpp.

#include "ecu/OpenDamos.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ecu {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

const std::uint8_t* romPtr(QByteArrayView rom) {
    return reinterpret_cast<const std::uint8_t*>(rom.data());
}

// Read a big-endian integer of the given type at off. Returns nullopt when the
// access would run past the ROM (mirrors the JS readInt returning null).
std::optional<int64_t> readInt(QByteArrayView rom, std::int64_t off,
                               DamosDataType type) {
    const auto sz = static_cast<std::int64_t>(damosTypeSize(type));
    if (off < 0 || off + sz > static_cast<std::int64_t>(rom.size()))
        return std::nullopt;
    const std::uint8_t* b = romPtr(rom) + off;
    switch (type) {
        case DamosDataType::UByte: return static_cast<int64_t>(b[0]);
        case DamosDataType::SByte: {
            int v = b[0];
            return static_cast<int64_t>(v & 0x80 ? v - 0x100 : v);
        }
        case DamosDataType::UWordBE:
            return static_cast<int64_t>((b[0] << 8) | b[1]);
        case DamosDataType::SWordBE: {
            int v = (b[0] << 8) | b[1];
            return static_cast<int64_t>(v & 0x8000 ? v - 0x10000 : v);
        }
        case DamosDataType::ULongBE:
            return (static_cast<int64_t>(b[0]) << 24) |
                   (static_cast<int64_t>(b[1]) << 16) |
                   (static_cast<int64_t>(b[2]) << 8) |
                   static_cast<int64_t>(b[3]);
        case DamosDataType::SLongBE: {
            std::int64_t u = (static_cast<int64_t>(b[0]) << 24) |
                             (static_cast<int64_t>(b[1]) << 16) |
                             (static_cast<int64_t>(b[2]) << 8) |
                             static_cast<int64_t>(b[3]);
            return (u & 0x80000000LL) ? u - 0x100000000LL : u;
        }
        case DamosDataType::UWordLE:
            return static_cast<int64_t>((b[1] << 8) | b[0]);
        case DamosDataType::SWordLE: {
            int v = (b[1] << 8) | b[0];
            return static_cast<int64_t>(v & 0x8000 ? v - 0x10000 : v);
        }
        case DamosDataType::ULongLE:
            return (static_cast<int64_t>(b[3]) << 24) |
                   (static_cast<int64_t>(b[2]) << 16) |
                   (static_cast<int64_t>(b[1]) << 8) |
                   static_cast<int64_t>(b[0]);
        case DamosDataType::SLongLE: {
            std::int64_t u = (static_cast<int64_t>(b[3]) << 24) |
                             (static_cast<int64_t>(b[2]) << 16) |
                             (static_cast<int64_t>(b[1]) << 8) |
                             static_cast<int64_t>(b[0]);
            return (u & 0x80000000LL) ? u - 0x100000000LL : u;
        }
    }
    return std::nullopt;
}

// Parse a "0x..." address string into a byte offset. The JS used
// parseInt(s, 16), i.e. hexadecimal regardless of a 0x prefix; we replicate
// that without throwing (std::from_chars is non-throwing).
std::int64_t parseAddr(const std::string& s) {
    if (s.empty()) return 0;
    std::string_view sv{s};
    if (sv.size() >= 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X'))
        sv.remove_prefix(2);
    std::int64_t value = 0;
    std::from_chars(sv.data(), sv.data() + sv.size(), value, 16);
    return value;
}

// Fuzzy axis matcher — two-pass: strict element-wise, then bag-of-values.
// Tolerates small drifts between close firmwares without reorganising axes.
AxisMatchResult axisMatches(const std::vector<int64_t>& actual,
                            const std::vector<int64_t>& expected,
                            const AxisTolerance& opts) {
    AxisMatchResult r;
    if (actual.size() != expected.size() || expected.empty())
        return r;

    // Phase 1: strict element-wise.
    int    strictMatches = 0;
    double totalErr = 0.0, totalAbs = 0.0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const double e = static_cast<double>(expected[i]);
        const double a = static_cast<double>(actual[i]);
        const double tol = std::max(opts.absTol, std::abs(e) * opts.relTol);
        const double err = std::abs(a - e);
        if (err <= tol) ++strictMatches;
        totalErr += err;
        totalAbs += std::abs(e);
    }
    const double strictFrac =
        static_cast<double>(strictMatches) / expected.size();
    if (strictFrac >= opts.strictMinFrac) {
        r.match   = true;
        r.score   = strictFrac *
                    (1.0 - std::min(1.0, totalErr / (totalAbs != 0.0 ? totalAbs : 1.0)));
        r.mode    = AxisMatchMode::Strict;
        r.matches = strictMatches;
        r.total   = static_cast<int>(expected.size());
        return r;
    }

    // Phase 2: bag-of-values — tolerates insertions/shifts.
    int bagMatches = 0;
    for (const auto eRaw : expected) {
        const double e = static_cast<double>(eRaw);
        const double tol = std::max(opts.absTol, std::abs(e) * opts.relTol);
        const bool found = std::any_of(
            actual.begin(), actual.end(), [&](int64_t aRaw) {
                return std::abs(static_cast<double>(aRaw) - e) <= tol;
            });
        if (found) ++bagMatches;
    }
    const double bagFrac = static_cast<double>(bagMatches) / expected.size();
    if (bagFrac >= opts.bagMinFrac) {
        // Sanity: min/max must line up within +/-15%, otherwise we risk
        // matching another map that happens to share 70% of the same numbers.
        const auto [eMinIt, eMaxIt] =
            std::minmax_element(expected.begin(), expected.end());
        const auto [aMinIt, aMaxIt] =
            std::minmax_element(actual.begin(), actual.end());
        const double eMin = static_cast<double>(*eMinIt);
        const double eMax = static_cast<double>(*eMaxIt);
        const double aMin = static_cast<double>(*aMinIt);
        const double aMax = static_cast<double>(*aMaxIt);
        constexpr double rangeTol = 0.15;
        const bool minOk =
            std::abs(aMin - eMin) <= std::max(opts.absTol, std::abs(eMin) * rangeTol);
        const bool maxOk =
            std::abs(aMax - eMax) <= std::max(opts.absTol, std::abs(eMax) * rangeTol);
        if (minOk && maxOk) {
            r.match   = true;
            r.score   = bagFrac * 0.8;
            r.mode    = AxisMatchMode::Bag;
            r.matches = bagMatches;
            r.total   = static_cast<int>(expected.size());
            return r;
        }
    }

    r.match = false;
    r.score = std::max(strictFrac, bagFrac) * 0.5;
    return r;
}

std::string modeName(AxisMatchMode m) {
    switch (m) {
        case AxisMatchMode::Strict: return "strict";
        case AxisMatchMode::Bag:    return "bag";
        case AxisMatchMode::None:   return "n/a";
    }
    return "n/a";
}

}  // namespace

// ---------------------------------------------------------------------------
// Fingerprint scan.
// ---------------------------------------------------------------------------

std::vector<FingerprintCandidate>
OpenDamos::findMapByFingerprint(QByteArrayView rom, const DamosEntry& entry,
                                const RelocateOptions& opts) {
    const bool isMap   = entry.type == DamosType::Map;
    const bool isCurve = entry.type == DamosType::Curve;
    if (!isMap && !isCurve) return {};
    if (entry.axes.empty()) return {};

    const std::int64_t headerBytes = isMap ? 4 : 2;
    const DamosDataType axisDT = entry.axes[0].dataType;
    const std::int64_t axisSize = static_cast<std::int64_t>(damosTypeSize(axisDT));

    // The inline (nx, ny) dimension header is stored in the same endianness as
    // the axes: big-endian for Bosch EDC16, little-endian for EDC17 (TriCore).
    const bool axisLE = axisDT == DamosDataType::SWordLE ||
                        axisDT == DamosDataType::UWordLE ||
                        axisDT == DamosDataType::SLongLE ||
                        axisDT == DamosDataType::ULongLE;
    const DamosDataType headerDT =
        axisLE ? DamosDataType::UWordLE : DamosDataType::UWordBE;

    const std::vector<int64_t>& xFp = entry.axes[0].fingerprint;
    const std::vector<int64_t>* yFp =
        (isMap && entry.axes.size() > 1) ? &entry.axes[1].fingerprint : nullptr;
    const int expectedNx = entry.dims.nx;
    const int expectedNy = isMap ? entry.dims.ny : 0;

    std::vector<FingerprintCandidate> candidates;
    const std::int64_t step = static_cast<std::int64_t>(opts.step ? opts.step : 2);
    const std::int64_t start =
        static_cast<std::int64_t>(opts.startOffset.value_or(0));
    const std::int64_t end =
        std::min<std::int64_t>(
            static_cast<std::int64_t>(opts.endOffset.value_or(rom.size())),
            rom.size());

    for (std::int64_t off = start; off <= end - headerBytes; off += step) {
        const auto nx = readInt(rom, off, headerDT);
        if (!nx || *nx != expectedNx) continue;
        if (isMap) {
            const auto ny = readInt(rom, off + 2, headerDT);
            if (!ny || *ny != expectedNy) continue;
        }

        const std::int64_t xAxisOff = off + headerBytes;
        std::vector<int64_t> xAxis;
        xAxis.reserve(expectedNx);
        bool ok = true;
        for (int i = 0; i < expectedNx; ++i) {
            const auto v = readInt(rom, xAxisOff + i * axisSize, axisDT);
            // JS pushes null on OOB; axisMatches then compares against NaN-ish
            // values. We bail out instead — an OOB axis can never match.
            if (!v) { ok = false; break; }
            xAxis.push_back(*v);
        }
        if (!ok) continue;
        const AxisMatchResult xResult = axisMatches(xAxis, xFp, opts.axisTol);
        if (!xResult.match) continue;

        AxisMatchResult yResult;
        yResult.match = true;
        yResult.score = 1.0;
        yResult.mode  = AxisMatchMode::None;
        std::vector<int64_t> yAxis;
        if (isMap) {
            const std::int64_t yAxisOff = xAxisOff + expectedNx * axisSize;
            yAxis.reserve(expectedNy);
            ok = true;
            for (int i = 0; i < expectedNy; ++i) {
                const auto v = readInt(rom, yAxisOff + i * axisSize, axisDT);
                if (!v) { ok = false; break; }
                yAxis.push_back(*v);
            }
            if (!ok) continue;
            yResult = axisMatches(yAxis, yFp ? *yFp : std::vector<int64_t>{},
                                  opts.axisTol);
            if (!yResult.match) continue;
        }

        // Composite score: mean of axis scores, +0.1 if both axes matched
        // strictly (maximum confidence).
        const double avgScore =
            isMap ? (xResult.score + yResult.score) / 2.0 : xResult.score;
        const double strictBonus =
            (xResult.mode == AxisMatchMode::Strict &&
             (!isMap || yResult.mode == AxisMatchMode::Strict))
                ? 0.1
                : 0.0;

        FingerprintCandidate cand;
        cand.address = static_cast<std::size_t>(off);
        cand.score   = std::min(1.0, avgScore + strictBonus);
        cand.xMode   = xResult.mode;
        cand.yMode   = yResult.mode;
        cand.xAxis   = std::move(xAxis);
        cand.yAxis   = std::move(yAxis);
        candidates.push_back(std::move(cand));
    }

    if (candidates.empty()) return {};

    // Sort: score desc, then proximity to defaultAddress (tie-breaker).
    const std::int64_t defaultAddr = parseAddr(entry.defaultAddress);
    std::sort(candidates.begin(), candidates.end(),
              [&](const FingerprintCandidate& a, const FingerprintCandidate& b) {
                  if (std::abs(b.score - a.score) > 0.01) return a.score > b.score;
                  return std::abs(static_cast<std::int64_t>(a.address) - defaultAddr) <
                         std::abs(static_cast<std::int64_t>(b.address) - defaultAddr);
              });

    return candidates;
}

// ---------------------------------------------------------------------------
// VALUE relocation by anchor.
// ---------------------------------------------------------------------------

std::optional<ValueMatch>
OpenDamos::findValueByAnchor(QByteArrayView rom, const DamosEntry& entry,
                             const DamosEntry& anchorEntry,
                             std::size_t anchorAddress) {
    const std::int64_t defaultAddr   = parseAddr(entry.defaultAddress);
    const std::int64_t anchorDefault = parseAddr(anchorEntry.defaultAddress);
    const std::int64_t delta =
        static_cast<std::int64_t>(anchorAddress) - anchorDefault;
    const std::int64_t baseCandidate = defaultAddr + delta;
    if (baseCandidate < 0 ||
        baseCandidate + 2 > static_cast<std::int64_t>(rom.size()))
        return std::nullopt;

    const DamosDataType dt = entry.data.dataType;
    const double factor = entry.data.factor;
    const double offset = entry.data.offset;

    const std::optional<std::string>& method = entry.relocation.method;
    const auto& valueRange = entry.relocation.valueRange;
    const std::size_t searchWindow =
        entry.relocation.searchWindow.value_or(4096); // +/-2KB default

    const auto baseRaw = readInt(rom, baseCandidate, dt);
    std::optional<double> basePhys;
    if (baseRaw) basePhys = static_cast<double>(*baseRaw) * factor + offset;
    const bool baseInRange =
        valueRange
            ? (basePhys && *basePhys >= (*valueRange)[0] &&
               *basePhys <= (*valueRange)[1])
            : true;

    // "value-range-search": find the best value near the anchor that falls
    // inside the expected range. "Best" = closest to stockPhysValue (or range
    // median if no stock).
    if (method && *method == "value-range-search" && valueRange) {
        const double targetPhys =
            entry.stockPhysValue
                ? *entry.stockPhysValue
                : ((*valueRange)[0] + (*valueRange)[1]) / 2.0;
        std::int64_t bestAddr = -1, bestRaw = 0;
        double bestPhys = 0.0, bestErr = kInf;
        const std::int64_t halfWin = static_cast<std::int64_t>(searchWindow / 2);
        const std::int64_t lo = std::max<std::int64_t>(0, baseCandidate - halfWin);
        const std::int64_t hi = std::min<std::int64_t>(
            static_cast<std::int64_t>(rom.size()) - 2, baseCandidate + halfWin);
        for (std::int64_t a = lo; a <= hi; a += 2) {
            const auto r = readInt(rom, a, dt);
            if (!r) continue;
            const double p = static_cast<double>(*r) * factor + offset;
            if (p < (*valueRange)[0] || p > (*valueRange)[1]) continue;
            const double err = std::abs(p - targetPhys);
            if (err < bestErr) {
                bestErr = err; bestAddr = a; bestRaw = *r; bestPhys = p;
            }
        }
        if (bestAddr >= 0) {
            ValueMatch m;
            m.address    = static_cast<std::size_t>(bestAddr);
            m.delta      = bestAddr - defaultAddr;
            m.raw        = bestRaw;
            m.physValue  = bestPhys;
            m.confidence = 0.7; // less reliable than a fingerprint, more than a raw anchor
            m.plausible  = true;
            m.method     = "value-range-search";
            return m;
        }
        // else fall through to anchor-delta on the anchored address.
    }

    // "anchor-delta" (default).
    const bool isPadding =
        baseRaw && (*baseRaw == -1 || *baseRaw == 0xFFFF ||
                    (*baseRaw == 0 && entry.stockRawValue &&
                     *entry.stockRawValue != 0));
    double confidence = isPadding ? 0.0 : 0.8;
    bool   plausible  = !isPadding;
    if (entry.stockRawValue && baseRaw) {
        const std::int64_t stock = *entry.stockRawValue;
        const double absTolRange =
            std::max(std::abs(static_cast<double>(stock)) * 10.0, 500.0);
        if (*baseRaw < 0 || *baseRaw > 32767) plausible = false;
        if (std::abs(static_cast<double>(*baseRaw - stock)) > absTolRange &&
            *baseRaw > 0) {
            confidence = 0.5;
        }
        if (!plausible) confidence = 0.0;
    }
    if (valueRange && basePhys) {
        if (!baseInRange) { confidence = 0.0; plausible = false; }
    }

    ValueMatch m;
    m.address    = static_cast<std::size_t>(baseCandidate);
    m.delta      = delta;
    m.raw        = baseRaw;
    m.physValue  = basePhys;
    m.confidence = confidence;
    m.plausible  = plausible;
    m.method     = "anchor-delta";
    return m;
}

// ---------------------------------------------------------------------------
// Full relocation.
// ---------------------------------------------------------------------------

std::vector<RelocResult>
OpenDamos::relocate(const DamosRecipe& recipe, QByteArrayView rom,
                    const RelocateOptions& opts) const {
    std::vector<RelocResult> result;

    // name -> chosen MAP/CURVE candidate (entry index + candidate).
    struct MapMatch {
        const DamosEntry*    entry = nullptr;
        FingerprintCandidate cand;
    };
    std::map<std::string, MapMatch> mapMatches;

    // Phase 1.a: collect MAP/CURVE candidates per entry (full lists).
    std::vector<const DamosEntry*> mapEntries;
    std::map<std::string, std::vector<FingerprintCandidate>> entryCandidates;
    for (const auto& c : recipe.characteristics) {
        if (c.type == DamosType::Map || c.type == DamosType::Curve) {
            mapEntries.push_back(&c);
            entryCandidates[c.name] = findMapByFingerprint(rom, c, opts);
        }
    }

    // Phase 1.b: greedy attribution. Entries are processed in recipe order
    // (significant: Hi before Lo, etc.); each takes its best candidate not yet
    // taken, preferring the one closest to its defaultAddress.
    std::map<std::size_t, bool> usedAddresses;
    std::map<std::string, std::optional<FingerprintCandidate>> resolvedByName;
    for (const DamosEntry* c : mapEntries) {
        auto& cands = entryCandidates[c->name];
        const std::int64_t defaultAddr = parseAddr(c->defaultAddress);

        std::vector<FingerprintCandidate> free;
        for (const auto& x : cands)
            if (!usedAddresses.contains(x.address)) free.push_back(x);

        if (free.empty()) {
            resolvedByName[c->name] = std::nullopt;
            continue;
        }
        std::sort(free.begin(), free.end(),
                  [&](const FingerprintCandidate& a, const FingerprintCandidate& b) {
                      if (std::abs(b.score - a.score) > 0.01) return a.score > b.score;
                      return std::abs(static_cast<std::int64_t>(a.address) - defaultAddr) <
                             std::abs(static_cast<std::int64_t>(b.address) - defaultAddr);
                  });
        const FingerprintCandidate pick = free.front();
        usedAddresses[pick.address] = true;
        resolvedByName[c->name]     = pick;
        mapMatches[c->name]         = MapMatch{ c, pick };
    }

    // Phase 1.c: format MAP/CURVE results.
    for (const DamosEntry* c : mapEntries) {
        const std::int64_t defaultAddr = parseAddr(c->defaultAddress);
        const auto& picked = resolvedByName[c->name];
        RelocResult r;
        r.name        = c->name;
        r.type        = c->type;
        r.category    = c->category;
        r.description = c->description;
        r.defaultAddress = static_cast<std::size_t>(defaultAddr);

        if (picked) {
            const auto& pick = *picked;
            r.address       = pick.address;
            r.delta         = static_cast<std::int64_t>(pick.address) - defaultAddr;
            r.addressSource = AddressSource::Fingerprint;
            r.matchMode =
                modeName(pick.xMode == AxisMatchMode::None ? AxisMatchMode::Strict
                                                           : pick.xMode);
            if (c->type == DamosType::Map)
                r.matchMode += "/" + modeName(pick.yMode == AxisMatchMode::None
                                                  ? AxisMatchMode::Strict
                                                  : pick.yMode);
            r.score = pick.score;
        } else {
            const std::size_t otherCands = entryCandidates[c->name].size();
            r.address       = static_cast<std::size_t>(defaultAddr);
            r.delta         = 0;
            r.addressSource = AddressSource::DefaultFallback;
            r.score         = 0.0;
            r.warning =
                otherCands
                    ? std::to_string(otherCands) +
                          " candidate(s) found but all taken by other entries "
                          "— raise disambiguation debug."
                    : std::string("Axis fingerprint not found in ROM — very "
                                  "divergent firmware or not an EDC16C34 PSA.");
        }
        result.push_back(std::move(r));
    }

    // Phase 2: VALUEs via anchoring on a found MAP. Accept the anchor only if
    // the read value passes the sanity check (plausible). Otherwise fall back
    // to defaultAddress with a warning.
    for (const auto& c : recipe.characteristics) {
        if (c.type != DamosType::Value) continue;

        const std::optional<std::string>& anchorName = c.relocation.anchorMap;
        const MapMatch* anchor = nullptr;
        if (anchorName) {
            auto it = mapMatches.find(*anchorName);
            if (it != mapMatches.end()) anchor = &it->second;
        }

        if (anchor) {
            const auto found =
                findValueByAnchor(rom, c, *anchor->entry, anchor->cand.address);
            if (found && found->plausible) {
                RelocResult r;
                r.name        = c.name;
                r.type        = c.type;
                r.category    = c.category;
                r.description = c.description;
                r.address     = found->address;
                r.defaultAddress =
                    static_cast<std::size_t>(parseAddr(c.defaultAddress));
                r.delta         = found->delta;
                r.addressSource = AddressSource::Anchor;
                r.anchorMap     = anchorName;
                r.score         = found->confidence;
                r.raw           = found->raw;
                r.physValue     = found->physValue;
                if (found->confidence < 0.8)
                    r.warning = "Anchored value diverges from stock damos — "
                                "check manually before patching.";
                result.push_back(std::move(r));
                continue;
            }
        }

        // Fallback: defaultAddress.
        const std::int64_t defaultAddr = parseAddr(c.defaultAddress);
        const auto rawAtDefault = readInt(rom, defaultAddr, c.data.dataType);
        const bool defaultIsPadding =
            rawAtDefault && (*rawAtDefault == -1 || *rawAtDefault == 0xFFFF);

        RelocResult r;
        r.name        = c.name;
        r.type        = c.type;
        r.category    = c.category;
        r.description = c.description;
        r.address     = static_cast<std::size_t>(defaultAddr);
        r.defaultAddress = static_cast<std::size_t>(defaultAddr);
        r.delta         = 0;
        r.addressSource = AddressSource::DefaultFallback;
        r.score         = defaultIsPadding ? 0.0 : 0.3;
        r.raw           = rawAtDefault;
        if (defaultIsPadding)
            r.warning = "Default address reads padding (FF FF) — VALUE probably "
                        "absent here. Do NOT apply EGR/popbang without manual "
                        "verification.";
        else if (anchorName)
            r.warning = "Anchor " + *anchorName +
                        " gave no plausible read; default address used (raw=" +
                        (rawAtDefault ? std::to_string(*rawAtDefault) : "null") +
                        ").";
        else
            r.warning = "No anchor defined.";
        result.push_back(std::move(r));
    }

    return result;
}

std::vector<RelocResult>
OpenDamos::relocate(QByteArrayView rom, const RelocateOptions& opts) const {
    if (!m_recipe) return {};
    return relocate(*m_recipe, rom, opts);
}

} // namespace ecu
