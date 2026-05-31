#pragma once
//
// ChecksumEngine — ECU flash checksum verification / correction.
//
// Reverse-engineered from the MPPS V21 `Check/Check001.dll` and `Check045.dll`
// modules (UPX-unpacked, C++Builder Win32). Those modules implement a
// CRC-16/ARC over a fixed ROM window with the stored checksum kept big-endian at
// a fixed offset, and *patch the image in place* when the recomputed value
// differs. See docs/mpps-checksums.md §3 for the full disassembly and offsets.
//
// Algorithm (FACT, table verified byte-exact against the binary):
//   CRC-16/ARC  (poly 0x8005, reflected 0xA001, init 0x0000, refin/refout=true,
//                xorout 0x0000)
//   32 KB image: window [0x0030, 0x7FEA), stored BE @ 0x7FEA
//   64 KB image: window [0x8041, 0xFFFA), stored BE @ 0xFFFA
//
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <optional>
#include <vector>

namespace ecu {

// Raw CRC-16/ARC over a byte range. Standalone, reusable.
uint16_t crc16Arc(std::span<const uint8_t> data);

// Identifies which fixed MPPS EDC-class layout an image uses, purely from size.
enum class EdcLayout {
    Unknown,
    Edc32k,   // 0x8000  bytes — Check001 32 KB branch
    Edc64k,   // 0x10000 bytes — Check001 64 KB branch
};

// --- EDC16C34 2 MB full-flash (PSA 1.6 HDi) — DETECTION ONLY, NO CORRECTION ---
//
// A genuine 2 MB EDC16C34 dump carries a self-describing checksum-descriptor
// table delimited by the 8-byte magic below. Each descriptor names a [start,end]
// flash region plus a stored 32-bit checksum. The exact checksum primitive Bosch
// uses over those regions has NOT been recovered (it is neither a plain 8/16/32-bit
// additive sum nor any catalogued CRC-32 over the obvious region — verified against
// two genuine factory dumps), and the MPPS `Check025.dll` module that handles this
// ECU family only accepts 0x23020 / 0x80000 / 0x100000 program images read in boot
// mode, never a 2 MB full-flash image. See docs/mpps-checksums.md §8.
//
// Because writing a wrong checksum bricks the ECU, the engine deliberately refuses
// to "correct" a 2 MB EDC16C34 image: mppsLayoutForSize() returns Unknown for it
// and correctMpps()/verifyMpps() return nullopt. This helper exists ONLY so the UI
// layer can recognise the image and show a clear "checksum unsupported — left
// untouched" message instead of silently doing nothing.
constexpr std::size_t kEdc16c34TwoMbSize = 0x200000; // 2,097,152 bytes
inline constexpr std::array<uint8_t, 8> kEdc16c34ChecksumMagic{
    0xFA, 0xDE, 0xCA, 0xFE, 0xCA, 0xFE, 0xAF, 0xFE};

// True iff `image` looks like a 2 MB EDC16C34 full-flash dump (correct size AND at
// least one checksum-descriptor magic present). Never modifies the image.
bool isEdc16c34TwoMb(std::span<const uint8_t> image);

struct ChecksumRegion {
    std::size_t start;        // first byte covered by the CRC
    std::size_t end;          // one past the last covered byte (== checksum off)
    std::size_t checksumOff;  // offset of the 16-bit stored checksum (big-endian)
};

// Returns the region/layout for a known MPPS EDC image size, or nullopt.
std::optional<ChecksumRegion> mppsRegionForSize(std::size_t imageSize);
EdcLayout                     mppsLayoutForSize(std::size_t imageSize);

struct ChecksumResult {
    uint16_t computed = 0;   // CRC the engine computed over the region
    uint16_t stored   = 0;   // CRC currently in the image (big-endian)
    bool     valid    = false;   // computed == stored
};

class ChecksumEngine {
public:
    // Read the stored big-endian checksum at `off` from `image`.
    static uint16_t readStoredBE(std::span<const uint8_t> image, std::size_t off);

    // Write `value` big-endian at `off` into `image`.
    static void writeStoredBE(std::span<uint8_t> image, std::size_t off,
                              uint16_t value);

    // Verify a single explicit region. Does not modify the image.
    static ChecksumResult verify(std::span<const uint8_t> image,
                                 const ChecksumRegion& region);

    // Verify using the auto-detected MPPS layout (by image size).
    // Returns nullopt if the size is not a recognised MPPS EDC layout.
    static std::optional<ChecksumResult>
    verifyMpps(std::span<const uint8_t> image);

    // Correct a single region in place (writes the recomputed CRC if it differs).
    // Returns the result *after* correction (valid==true on success).
    static ChecksumResult correct(std::span<uint8_t> image,
                                  const ChecksumRegion& region);

    // Correct using the auto-detected MPPS layout. Returns the number of
    // checksums fixed (0 if already valid), mirroring the DLL's return value,
    // or nullopt if the size is unrecognised.
    static std::optional<int> correctMpps(std::span<uint8_t> image);
};

} // namespace ecu
