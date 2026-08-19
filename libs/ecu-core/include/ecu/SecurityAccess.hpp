#pragma once
//
// SecurityAccess — calcul seed→key pour les protocoles UDS (0x27) et KWP2000.
//
// Algorithmes intégrés :
//   PSA / Stellantis   : Peugeot, Citroën, DS, Opel (reverse : ludwig-v, GPL-3.0)
//   VAG SA2            : VW, Audi, Seat, Skoda
//   Daimler Standard   : Mercedes, Smart
//   Bosch Generic      : XOR+rotate simple (EDC15, MP3, …)
//   Generic XOR        : fallback universel
//
// Usage :
//   auto result = ecu::SecurityAccess::compute(
//       ecu::SecurityAccess::Algo::PSA,
//       seed,        // 4 octets big-endian du service 0x27 / 0x2781
//       ecuKey       // clé 16 bits propre à l'ECU (0 si non requis)
//   );
//   if (result) sendKey(*result);
//
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ecu {

class SecurityAccess {
public:
    enum class Algo {
        PSA,            // PSA/Stellantis — seed 4B, clé ECU 2B requise
        VAG_SA2,        // VAG SA2 — seed 4B, pas de clé ECU externe
        Daimler,        // Daimler standard — seed 4B
        BoschGeneric,   // Bosch XOR+rotate simple (EDC15/ME7 old) — seed 2B ou 4B
        GenericXOR,     // XOR 0xFFFFFFFF fallback
    };

    struct AlgoInfo {
        Algo             algo;
        std::string_view name;
        std::string_view description;
        bool             requiresEcuKey;
        uint8_t          seedBytes;      // longueur seed attendue (2 ou 4)
        uint8_t          keyBytes;       // longueur key résultante
    };

    // Liste tous les algorithmes supportés.
    static const std::vector<AlgoInfo>& algorithms();

    // Calcule la key à partir du seed et de la clé ECU (ecuKey ignoré si non requis).
    // Retourne nullopt si les paramètres sont invalides.
    static std::optional<uint32_t> compute(Algo algo,
                                           uint32_t seed,
                                           uint16_t ecuKey = 0);

    // Résout un algo depuis son nom (insensible à la casse).
    static std::optional<Algo> fromName(std::string_view name);

    // Commandes KWP2000 PSA pour demander / envoyer le seed-key.
    // Niveau 1 = "download" (flash), niveau 3 = "config" (zones ECU).
    static constexpr uint8_t KWP_SEED_DOWNLOAD   = 0x81; // 2781
    static constexpr uint8_t KWP_KEY_DOWNLOAD     = 0x82; // 2782
    static constexpr uint8_t KWP_SEED_CONFIG      = 0x83; // 2783
    static constexpr uint8_t KWP_KEY_CONFIG       = 0x84; // 2784

    // Construit la trame KWP complète pour envoyer la key calculée.
    // Ex : buildKwpKeyFrame(KWP_KEY_CONFIG, 0xDEADBEEF) → "2784DEADBEEF"
    static std::string buildKwpKeyFrame(uint8_t subFunc, uint32_t key);

    // Construit la requête UDS 0x27 sendKey.
    // subLevel : 0x02 pour level 1, 0x04 pour level 2, etc.
    static std::string buildUdsKeyFrame(uint8_t subLevel, uint32_t key, uint8_t keyBytes = 4);

private:
    static uint32_t computePSA(uint32_t seed, uint16_t ecuKey);
    static uint32_t computeVAG_SA2(uint32_t seed);
    static uint32_t computeDaimler(uint32_t seed);
    static uint32_t computeBoschGeneric(uint32_t seed);
};

// Informations diag par constructeur : IDs CAN, protocole, sessions.
struct DiagProfile {
    std::string_view manufacturer; // "PSA", "VAG", "BMW", "Renault", …
    std::string_view protocol;     // "KWP_HAB", "KWP_IS", "UDS_CAN"
    uint32_t         canTxId;      // ID d'émission OBD→ECU
    uint32_t         canRxId;      // ID de réception ECU→OBD
    uint16_t         canBaudKbps;  // 125 (HAB) ou 500 (IS)
    bool             needsPinAdapter; // broches CAN-DIAG ≠ 6/14 (ex. PSA AEE2010 : 3/8)
    SecurityAccess::Algo secAlgo;
    std::string_view kwpSessionOpen;  // trame KWP pour ouvrir session diag
    std::string_view udsSessionOpen;  // trame UDS équivalente (peut être vide)
};

// Profils pré-remplis pour les constructeurs couverts.
const std::vector<DiagProfile>& diagProfiles();

// Retourne le profil correspondant à l'ID constructeur ou famille ECU.
std::optional<DiagProfile> diagProfileFor(std::string_view manufacturerOrFamily);

} // namespace ecu
