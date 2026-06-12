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

// Ensemble de PID live couramment utiles au tuning (RPM, charge, MAP/boost, MAF,
// températures, papillon, avance, lambda, tension…).
const QList<LivePid>& livePids();

// Requête ELM327 d'un PID mode 01 : pid 0x0C -> "010C".
QString pidRequest(std::uint8_t pid, std::uint8_t mode = 0x01);

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

// Décode les codes défaut d'une réponse mode 03 (ex. "43 01 01 33 ..."), gère le
// multi-lignes. Renvoie une liste de codes type "P0101", "U0121"...
QStringList decodeDtcs(const QString& elmText);

// Décode le VIN d'une réponse mode 09 PID 02 (multi-lignes ISO-TP).
QString decodeVin(const QString& elmText);

} // namespace ecu::obd2
