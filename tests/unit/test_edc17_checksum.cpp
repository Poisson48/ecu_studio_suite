#include <gtest/gtest.h>
#include "ecu/Edc17Checksum.hpp"

#include <cstdint>
#include <span>
#include <vector>

using namespace ecu;

namespace {

void w32(std::vector<std::uint8_t>& d, std::size_t o, std::uint32_t v) {
    d[o] = v & 0xFF; d[o+1] = (v>>8)&0xFF; d[o+2] = (v>>16)&0xFF; d[o+3] = (v>>24)&0xFF;
}

// Construit une image EDC17 synthétique : un bloc (id 0x30) avec 2 structures
// checksum — CRC32 sur [0x40..0x7F] et ADD32 sur [0x80..0xBF] — et le marqueur
// 0xDEADBEEF en fin de bloc. Reproduit le layout validé sur dumps réels.
std::vector<std::uint8_t> makeEdc17() {
    std::vector<std::uint8_t> d(0x200, 0);
    for (std::size_t i = 0x40; i < 0xC0; ++i)
        d[i] = static_cast<std::uint8_t>(i * 7 + 3);   // données non triviales

    const std::size_t blk = 0x100, size = 0x90;
    w32(d, blk + 0x00, 0x30);                 // id (customer block)
    w32(d, blk + 0x04, static_cast<std::uint32_t>(size));
    w32(d, blk + 0x2C, 2);                    // 2 structures checksum
    // cs0 : CRC32 sur 0x80000040..0x8000007F
    const std::size_t s0 = blk + 0x34;
    w32(d, s0 + 4,  0x80000040); w32(d, s0 + 8, 0x8000007F);
    w32(d, s0 + 12, 0xFADECAFE); w32(d, s0 + 16, 0); w32(d, s0 + 28, 0x00);
    // cs1 : ADD32 sur 0x80000080..0x800000BF
    const std::size_t s1 = blk + 0x54;
    w32(d, s1 + 4,  0x80000080); w32(d, s1 + 8, 0x800000BF);
    w32(d, s1 + 12, 0xFADECAFE); w32(d, s1 + 16, 0xCAFEAFFE); w32(d, s1 + 28, 0x01);
    w32(d, blk + size - 4, 0xDEADBEEF);       // marqueur de fin de bloc
    return d;
}

Edc17Result verify(const std::vector<std::uint8_t>& d) {
    return edc17Verify(std::span<const std::uint8_t>(d.data(), d.size()));
}

} // namespace

TEST(Edc17Checksum, DiscoversBlockAndStructures) {
    auto d = makeEdc17();
    auto r = verify(d);
    ASSERT_TRUE(r.isEdc17);
    ASSERT_EQ(r.blocks.size(), std::size_t{1});
    ASSERT_EQ(r.total(), 2);
    EXPECT_EQ(r.inBoundsCount(), 2);
    EXPECT_EQ(r.blocks[0].cs[0].algo, Edc17Algo::Crc32);
    EXPECT_EQ(r.blocks[0].cs[1].algo, Edc17Algo::Add32);
    EXPECT_EQ(r.blocks[0].cs[0].startOff, std::size_t{0x40});  // adresse masquée
}

TEST(Edc17Checksum, CorrectMakesAllValid) {
    auto d = makeEdc17();
    auto fixed = edc17Correct(std::span<std::uint8_t>(d.data(), d.size()));
    ASSERT_TRUE(fixed.has_value());
    EXPECT_GE(*fixed, 1);
    EXPECT_TRUE(verify(d).allValid());   // CRC32 + ADD32 tous valides après correction
}

TEST(Edc17Checksum, RoundTripCorruptThenCorrect) {
    auto d = makeEdc17();
    edc17Correct(std::span<std::uint8_t>(d.data(), d.size()));   // rend valide
    ASSERT_TRUE(verify(d).allValid());

    d[0x50] ^= 0xFF;                      // corrompt une donnée de la région CRC32
    EXPECT_FALSE(verify(d).allValid());

    auto fixed = edc17Correct(std::span<std::uint8_t>(d.data(), d.size()));
    ASSERT_TRUE(fixed.has_value());
    EXPECT_TRUE(verify(d).allValid());    // re-corrigé
}

TEST(Edc17Checksum, NotEdc17ReturnsNullopt) {
    std::vector<std::uint8_t> junk(0x200, 0xFF);   // pas de table de blocs
    auto fixed = edc17Correct(std::span<std::uint8_t>(junk.data(), junk.size()));
    EXPECT_FALSE(fixed.has_value());
    EXPECT_FALSE(verify(junk).isEdc17);
}
