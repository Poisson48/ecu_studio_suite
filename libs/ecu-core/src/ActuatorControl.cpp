#include "ecu/ActuatorControl.hpp"
#include <algorithm>
#include <cstdio>
#include <string>

namespace ecu {

// ── Table des routines connues ────────────────────────────────────────────────
//
// Sources :
//   - [KWP PSA documenté] arduino-psa-diag (ludwig-v) : reboot, flash
//   - [KWP PSA sniffé] forum PSA-COM / MPPS privé : rares actionneurs moteur
//   - [UDS NAC/RCC] dragouf/PSA-Arduino-NAC-RCC : actionneurs multimédia
//   - [À découvrir] sniff Diagbox via socketspy : actionneurs moteur EDC16/EDC17
//
// Convention : localId = 0x0000 signifie "Local ID non encore découvert"
// → la routine est listée pour documentation mais ne peut pas encore être envoyée.
//
const std::vector<ActuatorDef>& knownActuators()
{
    static const std::vector<ActuatorDef> kActuators = {

        // ── PSA / Stellantis — routines KWP documentées ───────────────────────
        {
            "psa_reboot_1",
            "Reboot ECU (méthode 1)",
            "",   // toutes familles PSA
            "PSA",
            'K', 0xA8, false,
            "KWP 31A800 — documenté arduino-psa-diag"
        },
        {
            "psa_reboot_2",
            "Reboot ECU (méthode 2)",
            "",
            "PSA",
            'K', 0xA8, false,
            "KWP 31A801 — documenté arduino-psa-diag"
        },
        {
            "psa_flash_erase",
            "Effacement flash .cal (download unlock requis)",
            "",
            "PSA",
            'K', 0x81, true,
            "KWP 318181F05A — tool signature F05A requise. DANGER : efface la ROM."
        },

        // ── PSA/Bosch EDC16 — service 0x30 IO Control (LID 01h-15h) ──────────────
        // Bosch EDC16 SCD documente service 0x30 avec LIDs 01h-15h pour les actionneurs IO.
        // Les LIDs ci-dessous viennent d'une documentation Bosch KDI (Fiat/Iveco) dont la
        // numérotation SCD est cohérente avec l'EDC16. À confirmer par sniff sur EDC16C34 PSA.
        // Source : Bosch SCD "InputOutputControlByLocalId SID$30", doc KDI 2504 TCR ECU Manual.
        {
            "bosch_edc16_egr_onoff",
            "Vanne EGR ON/OFF (service 30 LID 0x21)",
            "EDC16",
            "PSA",
            'K', 0x21, true,
            "Source : Bosch KDI SCD LID 0x21. À confirmer par sniff sur EDC16C34. "
            "Trame : 30 21 07 FF (forcer ouvert) | 30 21 00 (retour ECU)",
            0x30
        },
        {
            "bosch_edc16_injector1_stop",
            "Arrêt injection cylindre 1 (service 30 LID 0x10)",
            "EDC16",
            "",    // multi-constructeur (PSA, VAG, Fiat)
            'K', 0x10, true,
            "Source : Bosch KDI SCD LID 0x10. À confirmer. "
            "Trame : 30 10 07 FF | 30 10 00 pour rendre le contrôle à l'ECU",
            0x30
        },
        {
            "bosch_edc16_injector2_stop",
            "Arrêt injection cylindre 2 (service 30 LID 0x11)",
            "EDC16",
            "",
            'K', 0x11, true,
            "Source : Bosch KDI SCD LID 0x11.",
            0x30
        },
        {
            "bosch_edc16_injector3_stop",
            "Arrêt injection cylindre 3 (service 30 LID 0x12)",
            "EDC16",
            "",
            'K', 0x12, true,
            "Source : Bosch KDI SCD LID 0x12.",
            0x30
        },
        {
            "bosch_edc16_injector4_stop",
            "Arrêt injection cylindre 4 (service 30 LID 0x13)",
            "EDC16",
            "",
            'K', 0x13, true,
            "Source : Bosch KDI SCD LID 0x13.",
            0x30
        },
        {
            "bosch_edc16_dpf_regen_fast",
            "Régénération FAP rapide (service 30 LID 0x24)",
            "EDC16",
            "",
            'K', 0x24, true,
            "Source : Bosch KDI SCD LID 0x24. Conditions : T° FAP > 400°C, régime > 1200.",
            0x30
        },
        {
            "bosch_edc16_dpf_regen_full",
            "Régénération FAP complète (service 30 LID 0x25)",
            "EDC16",
            "",
            'K', 0x25, true,
            "Source : Bosch KDI SCD LID 0x25.",
            0x30
        },
        {
            "bosch_edc16_intake_throttle",
            "Volet admission ON/OFF (service 30 LID 0x2B)",
            "EDC16",
            "",
            'K', 0x2B, true,
            "Source : Bosch KDI SCD LID 0x2B.",
            0x30
        },
        // VNT/wastegate : aucun LID public trouvé sur EDC16 PSA ou générique Bosch.
        // Nécessite sniff Diagbox (socketspy + Berlingo).
        {
            "psa_edc16_vnt_wastegate",
            "Test actionneur wastegate / VGT (LID inconnu)",
            "EDC16",
            "PSA",
            'K', 0x00, true,
            "⚠ LID non trouvé dans aucune source publique. "
            "Priorité : sniff socketspy pendant test Diagbox sur le Berlingo."
        },

        // ── PSA EDC17 — actionneurs moteur (UDS) ───────────────────────────
        {
            "psa_edc17_egr_test",
            "Test actionneur EGR (UDS 0x2F / Routine ID inconnu)",
            "EDC17",
            "PSA",
            'U', 0x0000, true,
            "⚠ Routine ID UDS non trouvé. À sniff sur véhicule compatible."
        },
        {
            "psa_edc17_wastegate",
            "Test actionneur wastegate / VGT (UDS, ID inconnu)",
            "EDC17",
            "PSA",
            'U', 0x0000, true,
            "⚠ Routine ID UDS non trouvé."
        },

        // ── PSA Multimédia (NAC/RCC) — sniffés, documentés ───────────────────
        {
            "psa_nac_screen_off",
            "Écran noir (test affichage)",
            "NAC",
            "PSA",
            'U', 0xDF07, false,
            "UDS 3101DF07 — documenté dragouf/PSA-Arduino-NAC-RCC"
        },

        // ── VAG EDC16/EDC17 — UDS routines (partiellement documentées) ───────
        {
            "vag_edc16_swirl_test",
            "Test volets de swirl (EDC16 PD)",
            "EDC16",
            "VAG",
            'U', 0x0200, true,
            "Routine ID approximatif — varie selon ECU/année."
        },
        {
            "vag_edc17_dpf_regen",
            "Régénération forcée FAP (EDC17)",
            "EDC17",
            "VAG",
            'U', 0x0203, true,
            "Routine documentée VCDS — à confirmer avec UDS trace."
        },

        // ── BMW — UDS routines ────────────────────────────────────────────────
        {
            "bmw_edc16_dpf_regen",
            "Régénération forcée FAP (EDC16)",
            "EDC16",
            "BMW",
            'U', 0xA301, true,
            "Routine documentée ISTA — à confirmer."
        },
    };
    return kActuators;
}

std::vector<ActuatorDef> actuatorsFor(std::string_view ecuFamily,
                                      std::string_view manufacturer)
{
    std::vector<ActuatorDef> out;
    for (const auto& a : knownActuators()) {
        const bool famMatch  = ecuFamily.empty()    || a.ecuFamily.empty()    || a.ecuFamily == ecuFamily;
        const bool mfrMatch  = manufacturer.empty() || a.manufacturer.empty() || a.manufacturer == manufacturer;
        if (famMatch && mfrMatch)
            out.push_back(a);
    }
    return out;
}

// ── Construction de trames ────────────────────────────────────────────────────

std::string buildKwpRoutineFrame(uint8_t localId, bool start)
{
    char buf[8];
    // KWP : 31 <localId> <01=start | 02=stop>
    std::snprintf(buf, sizeof(buf), "31%02X%02X",
                  (unsigned)localId, start ? 0x01u : 0x02u);
    return buf;
}

std::string buildUdsRoutineFrame(uint16_t routineId, bool start)
{
    char buf[12];
    // UDS : 31 <01=start|02=stop> <routineId MSB> <routineId LSB>
    std::snprintf(buf, sizeof(buf), "31%02X%04X",
                  start ? 0x01u : 0x02u, (unsigned)routineId);
    return buf;
}

std::string buildKwpIOControlFrame(uint8_t localId, bool activate)
{
    char buf[12];
    if (activate)
        // 30 <LID> 07 FF — force actuator active (controlParameter=07=shortTermAdjustment)
        std::snprintf(buf, sizeof(buf), "30%02X07FF", (unsigned)localId);
    else
        // 30 <LID> 00 — returnControlToECU
        std::snprintf(buf, sizeof(buf), "30%02X00", (unsigned)localId);
    return buf;
}

} // namespace ecu
