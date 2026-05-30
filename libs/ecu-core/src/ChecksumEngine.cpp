#include "ecu/ChecksumEngine.hpp"

#include <array>

namespace ecu {

namespace {

// Compile-time CRC-16 table generation for the reflected polynomial 0xA001
// (== reversed 0x8005). This reproduces, byte-for-byte, the 512-byte table the
// MPPS Check001/Check045 DLLs embed at VA 0x40A118. See docs/mpps-checksums.md.
constexpr std::array<uint16_t, 256> makeCrc16ArcTable() {
    std::array<uint16_t, 256> t{};
    for (int i = 0; i < 256; ++i) {
        uint16_t c = static_cast<uint16_t>(i);
        for (int b = 0; b < 8; ++b) {
            c = (c & 1) ? static_cast<uint16_t>((c >> 1) ^ 0xA001)
                        : static_cast<uint16_t>(c >> 1);
        }
        t[static_cast<std::size_t>(i)] = c;
    }
    return t;
}

constexpr auto kCrc16ArcTable = makeCrc16ArcTable();

// Sanity-check the generated table against the exact bytes pulled from the
// binary (table[1]/[2]/[128]/[255]). If these ever fail the polynomial is wrong.
static_assert(kCrc16ArcTable[1] == 0xC0C1, "CRC-16/ARC table mismatch (poly)");
static_assert(kCrc16ArcTable[2] == 0xC181, "CRC-16/ARC table mismatch");
static_assert(kCrc16ArcTable[128] == 0xA001, "CRC-16/ARC table mismatch");
static_assert(kCrc16ArcTable[255] == 0x4040, "CRC-16/ARC table mismatch");

} // namespace

uint16_t crc16Arc(std::span<const uint8_t> data) {
    uint16_t crc = 0x0000;
    for (uint8_t byte : data) {
        const uint8_t idx = static_cast<uint8_t>((crc ^ byte) & 0xFF);
        crc = static_cast<uint16_t>((crc >> 8) ^ kCrc16ArcTable[idx]);
    }
    return crc;
}

EdcLayout mppsLayoutForSize(std::size_t imageSize) {
    switch (imageSize) {
    case 0x8000:  return EdcLayout::Edc32k;
    case 0x10000: return EdcLayout::Edc64k;
    default:      return EdcLayout::Unknown;
    }
}

std::optional<ChecksumRegion> mppsRegionForSize(std::size_t imageSize) {
    switch (mppsLayoutForSize(imageSize)) {
    case EdcLayout::Edc32k:
        // Check001 32 KB branch: window [0x0030, 0x7FEA), stored BE @ 0x7FEA.
        return ChecksumRegion{0x0030, 0x7FEA, 0x7FEA};
    case EdcLayout::Edc64k:
        // Check001 64 KB branch: window [0x8041, 0xFFFA), stored BE @ 0xFFFA.
        return ChecksumRegion{0x8041, 0xFFFA, 0xFFFA};
    case EdcLayout::Unknown:
    default:
        return std::nullopt;
    }
}

uint16_t ChecksumEngine::readStoredBE(std::span<const uint8_t> image,
                                      std::size_t off) {
    if (off + 1 >= image.size()) return 0;
    return static_cast<uint16_t>((image[off] << 8) | image[off + 1]);
}

void ChecksumEngine::writeStoredBE(std::span<uint8_t> image, std::size_t off,
                                   uint16_t value) {
    if (off + 1 >= image.size()) return;
    image[off]     = static_cast<uint8_t>((value >> 8) & 0xFF);
    image[off + 1] = static_cast<uint8_t>(value & 0xFF);
}

ChecksumResult ChecksumEngine::verify(std::span<const uint8_t> image,
                                      const ChecksumRegion& region) {
    ChecksumResult r;
    if (region.end > image.size() || region.start > region.end ||
        region.checksumOff + 1 >= image.size()) {
        return r; // invalid bounds -> valid=false, computed/stored 0
    }
    r.computed = crc16Arc(image.subspan(region.start, region.end - region.start));
    r.stored   = readStoredBE(image, region.checksumOff);
    r.valid    = (r.computed == r.stored);
    return r;
}

std::optional<ChecksumResult>
ChecksumEngine::verifyMpps(std::span<const uint8_t> image) {
    auto region = mppsRegionForSize(image.size());
    if (!region) return std::nullopt;
    return verify(image, *region);
}

ChecksumResult ChecksumEngine::correct(std::span<uint8_t> image,
                                       const ChecksumRegion& region) {
    ChecksumResult r = verify(image, region);
    if (!r.valid && region.checksumOff + 1 < image.size() &&
        region.end <= image.size() && region.start <= region.end) {
        writeStoredBE(image, region.checksumOff, r.computed);
        r.stored = r.computed;
        r.valid  = true;
    }
    return r;
}

std::optional<int> ChecksumEngine::correctMpps(std::span<uint8_t> image) {
    auto region = mppsRegionForSize(image.size());
    if (!region) return std::nullopt;
    ChecksumResult before = verify(image, *region);
    if (before.valid) return 0;
    correct(image, *region);
    return 1; // one checksum fixed, mirroring the DLL's fix-count return value
}

} // namespace ecu
