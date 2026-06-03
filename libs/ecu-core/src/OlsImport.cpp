// OlsImport.cpp — see OlsImport.hpp for the format notes and the extraction
// strategy. No exceptions: every failure returns std::unexpected(message).

#include "ecu/OlsImport.hpp"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace ecu {

namespace {

// Minimum/maximum plausible eprom size. EDC16/EDC17 calibration regions sit
// comfortably inside this band; it also rejects small-integer noise.
constexpr std::int64_t kMinRomSize = 0x40000;     // 256 KiB

std::uint32_t u32le(const QByteArray& d, std::int64_t o) {
    const auto* b = reinterpret_cast<const std::uint8_t*>(d.constData()) + o;
    return static_cast<std::uint32_t>(b[0]) |
           (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) |
           (static_cast<std::uint32_t>(b[3]) << 24);
}

bool isHexToken(const QByteArray& d, std::int64_t off, std::int64_t len) {
    if (len <= 0) return false;
    for (std::int64_t i = 0; i < len; ++i)
        if (!std::isxdigit(static_cast<unsigned char>(d.at(off + i)))) return false;
    return true;
}

// Read the eprom size from the WinOLS header: the first length-prefixed,
// all-hexadecimal token whose value is a plausible ROM size. Returns 0 if none.
std::int64_t parseEpromSize(const QByteArray& d) {
    const std::int64_t n   = d.size();
    const std::int64_t lim = std::min<std::int64_t>(0x1000, n);
    std::int64_t o = 0;
    while (o + 4 <= lim) {
        const std::uint32_t len = u32le(d, o);
        if (len >= 1 && len <= 64 && o + 4 + static_cast<std::int64_t>(len) <= n) {
            if (isHexToken(d, o + 4, len)) {
                const std::int64_t v =
                    std::strtoll(d.mid(o + 4, len).toStdString().c_str(), nullptr, 16);
                if (v >= kMinRomSize && v <= n) return v;
            }
            o += 4 + len;
        } else {
            ++o;
        }
    }
    return 0;
}

// All byte offsets where a little-endian uint32 equals `value`.
std::vector<std::int64_t> findU32(const QByteArray& d, std::uint32_t value) {
    std::vector<std::int64_t> out;
    const std::int64_t n = d.size();
    for (std::int64_t o = 0; o + 4 <= n; ++o)
        if (u32le(d, o) == value) out.push_back(o);
    return out;
}

// Given a known eprom size, locate the ROM block: among all in-bounds blocks
// prefixed by `size`, the ROM is the one whose interior holds the fewest copies
// of the size value (the map-record stream is saturated with it, the ROM blob
// is not). Tie-break: latest prefix. Returns the prefix offset or -1.
std::int64_t selectRomPrefix(const std::vector<std::int64_t>& occ,
                             std::int64_t size, std::int64_t fileSize) {
    std::int64_t best = -1, bestInside = -1;
    for (const std::int64_t p : occ) {
        const std::int64_t blkStart = p + 4;
        const std::int64_t blkEnd   = blkStart + size;
        if (blkEnd > fileSize) continue;
        // Count size-value occurrences strictly inside (blkStart, blkEnd).
        const auto lo = std::lower_bound(occ.begin(), occ.end(), blkStart);
        const auto hi = std::lower_bound(occ.begin(), occ.end(), blkEnd);
        const std::int64_t inside = std::distance(lo, hi);
        if (best < 0 || inside < bestInside ||
            (inside == bestInside && p > best)) {
            best = p; bestInside = inside;
        }
    }
    return best;
}

std::expected<QByteArray, std::string>
extract(const QByteArray& ols, const QString& filename) {
    const std::int64_t n = ols.size();
    if (n < 32 || !ols.startsWith(QByteArray("\x0b\x00\x00\x00WinOLS File", 15)))
        return std::unexpected(
            ("ols: '" + filename + "' is not a WinOLS .ols project").toStdString());

    std::int64_t size = parseEpromSize(ols);

    // Fallback: no ASCII size in the header — take the most frequent
    // page-aligned, plausibly-sized uint32 that has at least one in-bounds block.
    if (size == 0) {
        std::vector<std::pair<std::uint32_t, int>> tally; // tiny, brute but fine
        for (std::int64_t o = 0; o + 4 <= n; ++o) {
            const std::uint32_t v = u32le(ols, o);
            if (v >= kMinRomSize && v <= static_cast<std::uint32_t>(n) &&
                (v & 0xFFF) == 0) {
                bool found = false;
                for (auto& t : tally)
                    if (t.first == v) { ++t.second; found = true; break; }
                if (!found) tally.push_back({v, 1});
            }
        }
        std::sort(tally.begin(), tally.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        if (!tally.empty()) size = tally.front().first;
    }
    if (size == 0)
        return std::unexpected("ols: could not determine eprom size");

    const std::vector<std::int64_t> occ =
        findU32(ols, static_cast<std::uint32_t>(size));
    const std::int64_t prefix = selectRomPrefix(occ, size, n);
    if (prefix < 0)
        return std::unexpected("ols: no in-bounds eprom block for size " +
                               std::to_string(size));

    QByteArray rom = ols.mid(prefix + 4, static_cast<int>(size));
    if (rom.size() != static_cast<int>(size))
        return std::unexpected("ols: short read of eprom block");

    // Sanity: a real flash image is mostly binary with substantial 0xFF fill.
    const std::int64_t ffCount = std::count(rom.constBegin(), rom.constEnd(),
                                            static_cast<char>(0xFF));
    if (ffCount == rom.size())
        return std::unexpected("ols: extracted block is all-0xFF (empty)");

    return rom;
}

bool looksLikeLabel(const QByteArray& s) {
    if (s.size() < 3 || s.size() > 48) return false;
    bool hasAlpha = false, hasSep = false;
    for (int i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s.at(i));
        if (std::isalpha(c)) hasAlpha = true;
        else if (c == '_') hasSep = true;
        else if (!std::isdigit(c)) return false;
        if (i == 0 && !std::isalpha(c)) return false;
    }
    // ECU labels are CamelCase or contain an underscore (AccPed_trqEngHiGear_MAP,
    // Rail_pSetPointBase_MAP, …) — filters out plain words / units.
    if (hasSep) return hasAlpha;
    bool hasUpper = false, hasLower = false;
    for (char ch : s) {
        if (std::isupper(static_cast<unsigned char>(ch))) hasUpper = true;
        if (std::islower(static_cast<unsigned char>(ch))) hasLower = true;
    }
    return hasAlpha && hasUpper && hasLower;
}

} // namespace

std::expected<QByteArray, std::string>
olsExtractRom(const QByteArray& ols, const QString& filename) {
    return extract(ols, filename.isEmpty() ? QStringLiteral("<buffer>") : filename);
}

std::expected<QByteArray, std::string>
olsExtractRom(const QString& olsPath) {
    QFile f(olsPath);
    if (!f.open(QIODevice::ReadOnly))
        return std::unexpected(
            ("ols: cannot open " + olsPath).toStdString());
    return extract(f.readAll(), QFileInfo(olsPath).fileName());
}

std::expected<std::vector<OlsMapEntry>, std::string>
olsExtractMaps(const QString& olsPath) {
    QFile f(olsPath);
    if (!f.open(QIODevice::ReadOnly))
        return std::unexpected(("ols: cannot open " + olsPath).toStdString());
    const QByteArray d = f.readAll();
    const std::int64_t n = d.size();
    if (n < 32 || !d.startsWith(QByteArray("\x0b\x00\x00\x00WinOLS File", 15)))
        return std::unexpected("ols: not a WinOLS .ols project");

    // Walk the length-prefixed token stream up to the ROM block, collecting
    // unique label-like identifiers. Addresses/dims are not resolved (the OLS
    // 5.x record layout linking label -> address is undocumented).
    std::int64_t romPrefix = n;
    if (const std::int64_t size = parseEpromSize(d); size > 0) {
        const auto occ = findU32(d, static_cast<std::uint32_t>(size));
        if (const std::int64_t p = selectRomPrefix(occ, size, n); p >= 0)
            romPrefix = p;
    }

    std::vector<OlsMapEntry> maps;
    std::vector<std::string> seen;
    std::int64_t o = 0;
    while (o + 4 <= romPrefix) {
        const std::uint32_t len = u32le(d, o);
        if (len >= 3 && len <= 48 && o + 4 + static_cast<std::int64_t>(len) <= n) {
            const QByteArray s = d.mid(o + 4, len);
            if (looksLikeLabel(s)) {
                const std::string name = s.toStdString();
                if (std::find(seen.begin(), seen.end(), name) == seen.end()) {
                    seen.push_back(name);
                    OlsMapEntry e; e.name = name;
                    maps.push_back(std::move(e));
                }
            }
            o += 4 + len;
        } else {
            ++o;
        }
    }
    return maps;
}

} // namespace ecu
