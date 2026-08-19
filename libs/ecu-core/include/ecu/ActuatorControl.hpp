#pragma once
//
// ActuatorControl — IO Control (0x30) et Routine Control (0x31 / UDS 0x31)
//
// Pilotage des actionneurs ECU via les routines constructeur.
// Les Local IDs PSA sont propriétaires ; ils sont documentés ici au fur et à
// mesure qu'ils sont découverts par sniff CAN (Diagbox → socketspy).
//
// Deux services KWP utilisés :
//
//   SERVICE 0x30 — InputOutputControlByLocalIdentifier (IO Control)
//     Bosch EDC16 SCD : LIDs 01h–15h autorisés.
//     Trame start  : 30 <LID> 07 FF  (forcer état actif)
//     Trame stop   : 30 <LID> 00     (rendre contrôle à l'ECU)
//     Réponse OK   : 70 <LID> ...
//
//   SERVICE 0x31 — StartRoutineByLocalIdentifier (Routine Control)
//     Trame start  : 31 <LID> 01
//     Trame stop   : 32 <LID>
//     Résultat     : 33 <LID>
//     Réponse OK   : 71 <LID> 01
//
// Séquence requise avant d'envoyer :
//   1. Ouvrir session diag  : KWP 10C0 / UDS 10 03
//   2. Security Access      : SecurityAccess::compute() → 2783/2784 ou 27 03/04
//   3. Keep-alive 3E toutes 2s pendant toute la durée du test
//
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ecu {

// Statut d'une routine : attendu vs obtenu.
enum class RoutineStatus { Unknown, Running, Stopped, Rejected };

// Définition d'un actionneur / routine.
struct ActuatorDef {
    std::string_view id;          // identifiant interne ("wastegate_psa_edc16")
    std::string_view label;       // libellé affiché ("Wastegate / VGT")
    std::string_view ecuFamily;   // famille ECU ("EDC16", "EDC17", "ME7", ...)
    std::string_view manufacturer; // "PSA", "VAG", "BMW", ...
    uint8_t          protocol;    // 'K' = KWP, 'U' = UDS
    uint16_t         localId;     // Local ID de la routine (1 octet KWP, 2 octets UDS)
    bool             needsUnlock; // true = security access niveau 2 requis
    std::string_view notes;       // source, conditions, risques
    uint8_t          kwpService;  // KWP: 0x30=IOControl, 0x31=RoutineControl (défaut 0x31)
};

// Retourne toutes les routines connues.
const std::vector<ActuatorDef>& knownActuators();

// Filtre par famille ECU et/ou constructeur (vide = tous).
std::vector<ActuatorDef> actuatorsFor(std::string_view ecuFamily,
                                      std::string_view manufacturer = {});

// Construit la trame KWP service 0x31 (StartRoutineByLocalIdentifier).
// start=true → sous-fonction 01 (start), false → 02 (stop).
// Ex: buildKwpRoutineFrame(0xA8, true) → "31A801"
std::string buildKwpRoutineFrame(uint8_t localId, bool start = true);

// Construit la trame KWP service 0x30 (InputOutputControlByLocalIdentifier).
// activate=true → 30 <LID> 07 FF  (force actif)
// activate=false → 30 <LID> 00    (rend contrôle à l'ECU)
// Ex: buildKwpIOControlFrame(0x21, true) → "302107FF"
std::string buildKwpIOControlFrame(uint8_t localId, bool activate = true);

// Construit la trame UDS 31 01 / 31 02.
// routineId 2 octets, ex: buildUdsRoutineFrame(0xDF07, true) → "3101DF07"
std::string buildUdsRoutineFrame(uint16_t routineId, bool start = true);

} // namespace ecu
