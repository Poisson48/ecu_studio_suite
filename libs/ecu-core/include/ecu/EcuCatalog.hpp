#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ecu {

// Informations nécessaires pour dialoguer avec cet ECU en diag constructeur.
struct EcuDiagInfo {
    std::string_view diagManufacturer; // "PSA", "VAG", "BMW", "Renault", …
    std::string_view diagProtocol;     // "KWP_HAB" | "KWP_IS" | "UDS_CAN"
    uint32_t         canTxId;          // 0 = protocole K-Line / non CAN
    uint32_t         canRxId;
    uint16_t         canBaudKbps;      // 125 ou 500
    bool             needsPinAdapter;  // true = CAN-DIAG pas sur broches 6/14
    uint16_t         psaEcuKey;        // clé ECU PSA 2 octets (0 = inconnu)
    std::string_view openSessionCmd;   // trame hex d'ouverture de session
};

struct Stage1Map {
    std::string_view name;
    uint32_t         address;
    int              defaultPct;
    std::string_view label;
};

struct PopbangParam {
    uint32_t         address;
    int              min;
    int              max;
    std::string_view label;
};

struct PopbangParams {
    PopbangParam nOvrRun;
    PopbangParam qOvrRun;
};

struct AutoModPattern {
    std::string_view            id;
    std::span<const uint8_t>    search;
    std::span<const uint8_t>    replace;
    std::span<const uint8_t>    restore;
};

struct AutoModAddress {
    std::string_view                 id;
    uint32_t                         address;
    std::span<const uint8_t>         bytes;
    std::optional<std::span<const uint8_t>> restore;
    std::string_view                 note;
};

struct EcuEntry {
    std::string_view                      id;
    std::string_view                      name;
    std::string_view                      family;
    std::string_view                      fuel;
    std::string_view                      application;
    std::optional<std::string_view>       a2l;
    std::optional<std::span<const Stage1Map>>    stage1Maps;
    std::optional<PopbangParams>          popbangParams;
    std::optional<std::span<const AutoModPattern>> autoModPatterns;
    std::optional<std::span<const AutoModAddress>> autoModAddresses;
    std::optional<EcuDiagInfo>            diagInfo;  // accès constructeur
};

struct EcuSummary {
    std::string_view id;
    std::string_view name;
    std::string_view family;
    std::string_view fuel;
    std::string_view application;
    bool             hasA2l;
    bool             hasStage1;
    bool             hasPopbang;
    bool             hasDiagInfo;
};

std::span<const EcuEntry> catalog();

std::optional<EcuEntry>   getEcu(std::string_view id);

std::vector<EcuSummary>   listEcus();

} // namespace ecu
