// Edc17Checksum.cpp — voir Edc17Checksum.hpp. Algorithme + structure validés sur
// dumps réels (docs/ecu-research/edc17.md §0). Aucune exception.

#include "ecu/Edc17Checksum.hpp"

#include <algorithm>
#include <array>

namespace ecu {

namespace {

constexpr std::uint32_t kAddrMask   = 0x1FFFFFFFu;
constexpr std::uint32_t kDeadbeef   = 0xDEADBEEFu;
constexpr std::uint32_t kCrc32Poly  = 0xEDB88320u;   // IEEE 802.3 réfléchi
constexpr std::uint32_t kCrc32Expect = 0x35015001u;

bool isBlockId(std::uint32_t v) {
    switch (v & 0xFF) {
        case 0x10: case 0x30: case 0x40: case 0x60: case 0xC0: return true;
        default: return false;
    }
}

std::uint32_t u32le(std::span<const std::uint8_t> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    return static_cast<std::uint32_t>(d[o])
         | (static_cast<std::uint32_t>(d[o + 1]) << 8)
         | (static_cast<std::uint32_t>(d[o + 2]) << 16)
         | (static_cast<std::uint32_t>(d[o + 3]) << 24);
}
std::uint16_t u16le(std::span<const std::uint8_t> d, std::size_t o) {
    if (o + 2 > d.size()) return 0;
    return static_cast<std::uint16_t>(d[o] | (d[o + 1] << 8));
}
void w32le(std::span<std::uint8_t> d, std::size_t o, std::uint32_t v) {
    if (o + 4 > d.size()) return;
    d[o]     = static_cast<std::uint8_t>(v & 0xFF);
    d[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    d[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    d[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

// CRC32 d'un dword (bitwise, comme la routine TriCore de référence).
std::uint32_t crc32Dword(std::uint32_t crc, std::uint32_t dword) {
    for (int i = 0; i < 32; ++i) {
        const std::uint32_t x = (dword ^ crc) & 1u;
        dword >>= 1;
        crc = x ? (crc >> 1) ^ kCrc32Poly : (crc >> 1);
    }
    return crc;
}

// CRC32 sur les dwords LE de [start, endExcl). init = valeur de départ.
std::uint32_t crc32Dwords(std::span<const std::uint8_t> d,
                          std::size_t start, std::size_t endExcl, std::uint32_t init) {
    std::uint32_t crc = init;
    for (std::size_t p = start; p + 4 <= endExcl && p + 4 <= d.size(); p += 4)
        crc = crc32Dword(crc, u32le(d, p));
    return crc;
}

std::uint32_t add32Dwords(std::span<const std::uint8_t> d,
                          std::size_t start, std::size_t endExcl, std::uint32_t init) {
    std::uint32_t s = init;
    for (std::size_t p = start; p + 4 <= endExcl && p + 4 <= d.size(); p += 4)
        s = (s + u32le(d, p)) & 0xFFFFFFFFu;
    return s;
}

// ADD16 (rare) : somme de mots LE, dernier mot ajouté en poids fort (réf. medc17).
std::uint32_t add16Words(std::span<const std::uint8_t> d,
                         std::size_t start, std::size_t endIncl, std::uint32_t init) {
    std::uint32_t s = init;
    std::size_t p = start;
    for (; p + 2 <= endIncl && p + 2 <= d.size(); p += 2)
        s = (s + u16le(d, p)) & 0xFFFFFFFFu;
    if (p + 2 <= d.size())
        s = (s + (static_cast<std::uint32_t>(u16le(d, p)) << 16)) & 0xFFFFFFFFu;
    return s;
}

Edc17Algo algoFromByte(std::uint8_t a) {
    switch (a) {
        case 0x00: return Edc17Algo::Crc32;
        case 0x01: return Edc17Algo::Add32;
        case 0x10: return Edc17Algo::Add16;
        default:   return Edc17Algo::Unknown;
    }
}

// Découverte des blocs : scan dword-aligné.
std::vector<Edc17Block> findBlocks(std::span<const std::uint8_t> d) {
    std::vector<Edc17Block> out;
    const std::size_t n = d.size();
    std::size_t o = 0;
    while (o + 0x38 <= n) {
        const std::uint32_t id   = u32le(d, o);
        const std::uint32_t size = u32le(d, o + 4);
        if (isBlockId(id) && size >= 0x40 && size <= n - o &&
            u32le(d, o + size - 4) == kDeadbeef) {
            Edc17Block b;
            b.id = id; b.fileOff = o; b.size = size;
            const std::uint32_t numCs = u32le(d, o + 0x2C);
            if (numCs <= 64) {
                for (std::uint32_t k = 0; k < numCs; ++k) {
                    const std::size_t so = o + 0x34 + 32u * k;
                    if (so + 32 > n) break;
                    Edc17Cs cs;
                    cs.structOff = so;
                    cs.startAddr = u32le(d, so + 4);
                    cs.endAddr   = u32le(d, so + 8);
                    cs.init      = u32le(d, so + 12);
                    cs.expected  = u32le(d, so + 16);
                    cs.algo      = algoFromByte(static_cast<std::uint8_t>(u16le(d, so + 28) & 0xFF));
                    cs.startOff  = cs.startAddr & kAddrMask;
                    cs.endOff    = cs.endAddr   & kAddrMask;
                    cs.inBounds  = (cs.startOff < cs.endOff && cs.endOff < n);
                    b.cs.push_back(cs);
                }
            }
            out.push_back(std::move(b));
            o += size;
        } else {
            o += 4;
        }
    }
    return out;
}

// Calcule le checksum d'une structure et le valide.
void computeCs(std::span<const std::uint8_t> d, Edc17Cs& cs) {
    if (!cs.inBounds || cs.algo == Edc17Algo::Unknown) { cs.valid = false; return; }
    const std::size_t endExcl = cs.endOff + 1;  // end est inclusif
    switch (cs.algo) {
        case Edc17Algo::Crc32:
            cs.computed = crc32Dwords(d, cs.startOff, endExcl, cs.init);
            cs.valid = (cs.computed == kCrc32Expect);
            break;
        case Edc17Algo::Add32:
            cs.computed = add32Dwords(d, cs.startOff, endExcl, cs.init);
            cs.valid = (cs.computed == cs.expected);
            break;
        case Edc17Algo::Add16:
            cs.computed = add16Words(d, cs.startOff, cs.endOff, cs.init);
            cs.valid = (cs.computed == cs.expected);
            break;
        default: cs.valid = false;
    }
}

// Résout M·x = b sur GF(2) ; col[j] = effet du bit j de x. Renvoie nullopt si
// pas de solution.
std::optional<std::uint32_t> gf2Solve(const std::array<std::uint32_t, 32>& col,
                                      std::uint32_t target) {
    struct Eq { std::uint32_t coef; std::uint32_t rhs; };
    std::array<Eq, 32> eq{};
    for (int i = 0; i < 32; ++i) {
        std::uint32_t c = 0;
        for (int j = 0; j < 32; ++j)
            if ((col[j] >> i) & 1u) c |= (1u << j);
        eq[i] = { c, (target >> i) & 1u };
    }
    std::array<int, 32> pivotCol{}; pivotCol.fill(-1);
    int row = 0;
    for (int c = 0; c < 32 && row < 32; ++c) {
        int sel = -1;
        for (int r = row; r < 32; ++r) if ((eq[r].coef >> c) & 1u) { sel = r; break; }
        if (sel < 0) continue;
        std::swap(eq[row], eq[sel]);
        for (int r = 0; r < 32; ++r)
            if (r != row && ((eq[r].coef >> c) & 1u)) {
                eq[r].coef ^= eq[row].coef;
                eq[r].rhs  ^= eq[row].rhs;
            }
        pivotCol[row] = c; ++row;
    }
    for (int r = 0; r < 32; ++r)
        if (eq[r].coef == 0 && eq[r].rhs == 1) return std::nullopt;  // incohérent
    std::uint32_t x = 0;
    for (int r = 0; r < row; ++r)
        if (eq[r].rhs) x |= (1u << pivotCol[r]);
    return x;
}

// Corrige une structure en place (patch = dernier dword de la région). Renvoie
// true si la structure est valide après correction.
bool correctCs(std::span<std::uint8_t> d, Edc17Cs& cs) {
    if (!cs.inBounds) return false;
    const std::size_t patchOff = cs.endOff - 3;            // dernier dword de [start..end]
    if (patchOff < cs.startOff || patchOff + 4 > d.size()) return false;

    if (cs.algo == Edc17Algo::Add32) {
        // sum + diff == expected en ajustant le dernier dword.
        const std::uint32_t cur  = add32Dwords(d, cs.startOff, cs.endOff + 1, cs.init);
        const std::uint32_t diff = (cs.expected - cur) & 0xFFFFFFFFu;
        w32le(d, patchOff, (u32le(d, patchOff) + diff) & 0xFFFFFFFFu);
    } else if (cs.algo == Edc17Algo::Crc32) {
        // CRC partiel jusqu'AVANT le dernier dword (calculé une seule fois), puis
        // on résout le dernier dword pour atteindre 0x35015001.
        const std::uint32_t crcBefore =
            crc32Dwords(d, cs.startOff, patchOff, cs.init);
        const std::uint32_t crcZero = crc32Dword(crcBefore, 0);
        std::array<std::uint32_t, 32> col{};
        for (int j = 0; j < 32; ++j)
            col[j] = crc32Dword(crcBefore, 1u << j) ^ crcZero;
        auto x = gf2Solve(col, kCrc32Expect ^ crcZero);
        if (!x) return false;
        w32le(d, patchOff, *x);
    } else {
        return false;   // ADD16 / inconnu : non corrigé (non validé) — laissé tel quel
    }

    Edc17Cs after = cs;
    computeCs(d, after);
    cs.computed = after.computed;
    cs.valid    = after.valid;
    return cs.valid;
}

} // namespace

int  Edc17Result::total() const {
    int t = 0; for (const auto& b : blocks) t += static_cast<int>(b.cs.size()); return t;
}
int  Edc17Result::validCount() const {
    int v = 0; for (const auto& b : blocks) for (const auto& c : b.cs) v += c.valid; return v;
}
int  Edc17Result::inBoundsCount() const {
    int v = 0; for (const auto& b : blocks) for (const auto& c : b.cs) v += c.inBounds; return v;
}
bool Edc17Result::allValid() const {
    for (const auto& b : blocks)
        for (const auto& c : b.cs)
            if (c.inBounds && !c.valid) return false;
    return true;
}

Edc17Result edc17Verify(std::span<const std::uint8_t> image) {
    Edc17Result res;
    res.blocks = findBlocks(image);
    for (auto& b : res.blocks)
        for (auto& cs : b.cs)
            computeCs(image, cs);
    res.isEdc17 = res.total() > 0;
    return res;
}

std::optional<int> edc17Correct(std::span<std::uint8_t> image) {
    std::span<const std::uint8_t> cimg(image.data(), image.size());
    auto blocks = findBlocks(cimg);
    int total = 0;
    for (const auto& b : blocks) total += static_cast<int>(b.cs.size());
    if (total == 0) return std::nullopt;   // pas une EDC17

    // Rassemble toutes les structures, calcule leur état, et corrige des PLUS
    // PETITES régions aux plus grandes : une région englobante (ex. dataset complet)
    // est ainsi recalculée APRÈS ses sous-régions, sur le contenu déjà corrigé.
    std::vector<Edc17Cs> all;
    for (auto& b : blocks)
        for (auto& cs : b.cs) { computeCs(cimg, cs); all.push_back(cs); }
    std::sort(all.begin(), all.end(), [](const Edc17Cs& a, const Edc17Cs& b) {
        return (a.endOff - a.startOff) < (b.endOff - b.startOff);
    });

    int fixed = 0;
    for (auto& cs : all) {
        if (!cs.inBounds || cs.valid) continue;
        if (correctCs(image, cs)) ++fixed;
    }
    return fixed;
}

} // namespace ecu
