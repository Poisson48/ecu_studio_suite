#pragma once
//
// Obd2 — couche protocole OBD-II (SAE J1979) pour piloter un ELM327.
//
// Le matériel (port série / ELM327) est géré ailleurs (driver). Ce module est PUR
// et testable : il fabrique les requêtes PID, parse les réponses ASCII de l'ELM327
// (avec ou sans header CAN), interprète les PID mode 01 en valeurs physiques, et
// décode les codes défaut (mode 03) + le VIN (mode 09). Formules = standard J1979.
//
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>
#include <optional>

namespace ecu::obd2 {

// Un PID mode 01 « live » exposé dans le tableau de bord.
struct LivePid {
    std::uint8_t pid;
    const char*  name;
    const char*  unit;
};

// Ensemble de PID live utiles au tuning. Inclut tous les PID pour lesquels
// interpret() a une formule — le filtrage ECU se fait via
// decodeSupportedPidBitmap / probeSupportedPids.
const QList<LivePid>& livePids();

// Décode le bitmap « PID supportés » (réponse à 01 00 / 01 20 / 01 40 / 01 60).
// basePid = 0x00 → bits pour 0x01..0x20 ; 0x20 → 0x21..0x40 ; etc.
// Si le bit « next bitmap » (PID base+0x20) est set, nextBitmap=true.
QList<std::uint8_t> decodeSupportedPidBitmap(std::uint8_t basePid,
                                             const std::uint8_t* data,
                                             std::uint8_t len,
                                             bool* nextBitmap = nullptr);

// Requête ELM327 d'un PID mode 01 : pid 0x0C -> "010C".
QString pidRequest(std::uint8_t pid, std::uint8_t mode = 0x01);

// Requête freeze frame mode 02 (frame 0 par défaut) : pid 0x0B -> "02000B".
QString freezeFrameRequest(std::uint8_t pid, std::uint8_t frame = 0);

// PID mode 02 « freeze frame » utiles au diagnostic turbo / charge.
const QList<LivePid>& freezeFramePids();

// Résultat de parsing d'une réponse OBD-II.
struct Obd2Resp {
    bool                    ok   = false;
    std::uint8_t            mode = 0;
    std::uint8_t            pid  = 0;
    std::array<std::uint8_t, 8> data{};
    std::uint8_t            len  = 0;
};

// Parse une réponse ELM327 (ex. "41 0C 1A F8", "7E8 06 41 0C 1A F8 00 00",
// ou multi-lignes). Cherche la paire [mode+0x40, pid] attendue et renvoie les
// octets de données qui suivent. Robuste aux headers CAN 11/29 bits et au préfixe
// de longueur ISO-TP.
Obd2Resp parseResponse(const QString& elmText, std::uint8_t wantMode, std::uint8_t wantPid);

// Interprète un PID mode 01 en valeur physique (J1979). nullopt si PID non géré.
std::optional<double> interpret(std::uint8_t pid, const std::uint8_t* data, std::uint8_t len);

QString pidName(std::uint8_t pid);
QString pidUnit(std::uint8_t pid);

// Décode les codes défaut d'une réponse ELM327. Renvoie une liste de codes type
// "P0101", "U0121"...
//
// `mode` vaut 0x03 (défauts mémorisés) ou 0x07 (défauts en attente) ; l'octet de
// réponse attendu est mode+0x40. Gère les deux formes de multi-trames : KWP/ISO
// (chaque ligne rouvre par l'octet de réponse) et ISO-TP/CAN (lignes de
// continuation sans en-tête).
QStringList decodeDtcs(const QString& elmText, std::uint8_t mode = 0x03);

// Décode le VIN d'une réponse mode 09 PID 02 (multi-lignes ISO-TP).
QString decodeVin(const QString& elmText);

} // namespace ecu::obd2
