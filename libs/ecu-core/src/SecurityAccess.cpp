#include "ecu/SecurityAccess.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace ecu {

// ── Algorithmes internes ──────────────────────────────────────────────────────

// PSA/Stellantis — reverse par Wouter Bokslag & Jason F. (prototux),
// implémenté par ludwig-v. Licence GPL-3.0.
// Seed : 4 octets. Clé ECU : 2 octets (propre à chaque module, extractible
// depuis les fichiers .cal ou par bruteforce 65 536 itérations).
uint32_t SecurityAccess::computePSA(uint32_t seed, uint16_t ecuKey)
{
    auto transform = [](int16_t data, const uint8_t sec[3]) -> int16_t {
        int32_t r = ((int32_t)(data % sec[0]) * sec[2])
                  - ((int32_t)(data / sec[0]) * sec[1]);
        if (r < 0) r += (int32_t)(sec[0] * sec[2]) + sec[1];
        return (int16_t)r;
    };

    const uint8_t sec1[3] = { 0xB2, 0x3F, 0xAA };
    const uint8_t sec2[3] = { 0xB1, 0x02, 0xAB };

    const uint8_t pin[2]  = { (uint8_t)(ecuKey >> 8), (uint8_t)(ecuKey & 0xFF) };
    const uint8_t chg[4]  = {
        (uint8_t)(seed >> 24), (uint8_t)(seed >> 16),
        (uint8_t)(seed >>  8), (uint8_t)(seed & 0xFF)
    };

    const int16_t resMsb = transform((int16_t)((pin[0] << 8) | pin[1]), sec1)
                         | transform((int16_t)((chg[0] << 8) | chg[3]), sec2);
    const int16_t resLsb = transform((int16_t)((chg[1] << 8) | chg[2]), sec1)
                         | transform(resMsb, sec2);

    return ((uint32_t)(uint16_t)resMsb << 16) | (uint16_t)resLsb;
}

// VAG SA2 — basé sur bri3d/sa2_seed_key.
// Implémentation simplifiée de référence (LFSR + transformations linéaires).
uint32_t SecurityAccess::computeVAG_SA2(uint32_t seed)
{
    // Polynôme SA2 standard (30 bits)
    static const uint8_t kScript[] = {
        0x68, 0x02, 0x81, 0x49, 0x93, 0xa5, 0x5a, 0x55,
        0xaa, 0x4a, 0x05, 0x87, 0x81, 0x05, 0x95, 0x26,
        0x68, 0x05, 0x82, 0x49, 0x84, 0x5a, 0xa5, 0xaa,
        0x55, 0x87, 0x03, 0xf7, 0x80, 0x6a, 0x4c
    };

    uint32_t val = seed;
    for (const uint8_t b : kScript) {
        const uint8_t op  = (b >> 4) & 0x0F;
        const uint8_t arg = b & 0x0F;
        switch (op) {
            case 0: val = (val >> arg) | (val << (32 - arg)); break;
            case 1: val = (val << arg) | (val >> (32 - arg)); break;
            case 2: val ^= ((uint32_t)1 << arg); break;
            case 3: val = ~val; break;
            case 4: val += arg; break;
            case 5: val -= arg; break;
            case 6: val += 0xFE; break;
            case 7: val = (val >> arg); break;
            case 8: val = (val << arg); break;
            default: break;
        }
    }
    return val;
}

// Daimler Standard — reverse communautaire.
uint32_t SecurityAccess::computeDaimler(uint32_t seed)
{
    uint32_t key = seed;
    key += 0x28; key ^= 0x4A; key = (key << 2) | (key >> 30);
    key ^= 0x3E; key -= 0x5D; key = (key >> 3) | (key << 29);
    key += 0x0A; key ^= 0xA5;
    return key & 0xFFFF; // Daimler retourne 2 octets
}

// Bosch Generic — XOR+rotate simple, EDC15 / MP3.x / ME7 anciens.
uint32_t SecurityAccess::computeBoschGeneric(uint32_t seed)
{
    uint32_t key = seed ^ 0x55AA55AA;
    key = (key << 5) | (key >> 27);
    key ^= 0xA5A5A5A5;
    return key;
}

// ── Table des algos ───────────────────────────────────────────────────────────

const std::vector<SecurityAccess::AlgoInfo>& SecurityAccess::algorithms()
{
    static const std::vector<AlgoInfo> kAlgos = {
        { Algo::PSA,          "PSA",          "PSA/Stellantis (Peugeot, Citroën, DS, Opel)",    true,  4, 4 },
        { Algo::VAG_SA2,      "VAG_SA2",       "VAG SA2 (VW, Audi, Seat, Skoda)",                false, 4, 4 },
        { Algo::Daimler,      "Daimler",       "Daimler Standard (Mercedes, Smart)",             false, 4, 2 },
        { Algo::BoschGeneric, "BoschGeneric",  "Bosch générique XOR+rotate (EDC15, MP3, ME7)",   false, 4, 4 },
        { Algo::GenericXOR,   "GenericXOR",    "Générique XOR 0xFFFFFFFF (fallback)",            false, 4, 4 },
    };
    return kAlgos;
}

std::optional<SecurityAccess::Algo> SecurityAccess::fromName(std::string_view name)
{
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    for (const auto& a : algorithms()) {
        std::string aLower(a.name);
        std::transform(aLower.begin(), aLower.end(), aLower.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (aLower == lower) return a.algo;
    }
    return std::nullopt;
}

std::optional<uint32_t> SecurityAccess::compute(Algo algo,
                                                 uint32_t seed,
                                                 uint16_t ecuKey)
{
    switch (algo) {
        case Algo::PSA:
            return computePSA(seed, ecuKey);
        case Algo::VAG_SA2:
            return computeVAG_SA2(seed);
        case Algo::Daimler:
            return computeDaimler(seed);
        case Algo::BoschGeneric:
            return computeBoschGeneric(seed);
        case Algo::GenericXOR:
            return seed ^ 0xFFFFFFFFu;
    }
    return std::nullopt;
}

// ── Construction de trames ────────────────────────────────────────────────────

std::string SecurityAccess::buildKwpKeyFrame(uint8_t subFunc, uint32_t key)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "27%02X%08X",
                  (unsigned)subFunc, (unsigned)key);
    return buf;
}

std::string SecurityAccess::buildUdsKeyFrame(uint8_t subLevel,
                                              uint32_t key,
                                              uint8_t keyBytes)
{
    // UDS : 27 <subLevel> <key bytes>
    std::string out;
    char tmp[4];
    std::snprintf(tmp, sizeof(tmp), "27%02X", (unsigned)subLevel);
    out = tmp;
    const int shift = (keyBytes - 1) * 8;
    for (int i = shift; i >= 0; i -= 8) {
        std::snprintf(tmp, sizeof(tmp), "%02X", (unsigned)((key >> i) & 0xFF));
        out += tmp;
    }
    return out;
}

// ── Profils constructeurs ─────────────────────────────────────────────────────

const std::vector<DiagProfile>& diagProfiles()
{
    static const std::vector<DiagProfile> kProfiles = {
        // PSA AEE2010 (pré-2018) — KWP2000 HAB 125 kbps, CAN-DIAG broches 3/8
        {
            "PSA",
            "KWP_HAB",
            0x212, 0x652,    // TX / RX BSI
            125,
            true,            // adaptateur requis : brancher 3/8 sur 6/14
            SecurityAccess::Algo::PSA,
            "10C0",          // Open diagnostic session
            ""
        },
        // PSA NEA2020 (post-2018) — UDS CAN 500 kbps, broches standard 6/14
        {
            "PSA_NEA",
            "UDS_CAN",
            0x764, 0x664,    // TX / RX BSI (CAN-DIAG PSA standard)
            500,
            false,
            SecurityAccess::Algo::PSA,
            "",
            "10 03"          // UDS DiagnosticSessionControl extended
        },
        // VAG MQB/PQ — UDS CAN 500 kbps, IDs standard OBD
        {
            "VAG",
            "UDS_CAN",
            0x7E0, 0x7E8,
            500,
            false,
            SecurityAccess::Algo::VAG_SA2,
            "",
            "10 03"
        },
        // Renault (pré-gateway) — KWP2000 IS 500 kbps
        {
            "Renault",
            "KWP_IS",
            0x7C0, 0x7C8,
            500,
            false,
            SecurityAccess::Algo::GenericXOR,  // algo Renault non public
            "10 92",
            ""
        },
        // BMW E-series / F-series — UDS CAN 500 kbps
        {
            "BMW",
            "UDS_CAN",
            0x6F1, 0x600,
            500,
            false,
            SecurityAccess::Algo::Daimler,     // approx. — varie par ECU
            "",
            "10 03"
        },
        // Daimler (Mercedes) — UDS CAN 500 kbps
        {
            "Daimler",
            "UDS_CAN",
            0x7E0, 0x7E8,
            500,
            false,
            SecurityAccess::Algo::Daimler,
            "",
            "10 03"
        },
    };
    return kProfiles;
}

std::optional<DiagProfile> diagProfileFor(std::string_view key)
{
    std::string lower(key);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    for (const auto& p : diagProfiles()) {
        std::string mfr(p.manufacturer);
        std::transform(mfr.begin(), mfr.end(), mfr.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (mfr == lower || lower.find(mfr) != std::string::npos)
            return p;
    }
    return std::nullopt;
}

} // namespace ecu
