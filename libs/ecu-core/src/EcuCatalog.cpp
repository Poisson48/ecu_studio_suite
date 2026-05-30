#include "ecu/EcuCatalog.hpp"
#include <algorithm>

namespace ecu {

// ── EDC16C34 data ────────────────────────────────────────────────────────────

static constexpr Stage1Map kEdc16c34_Stage1[] = {
    { "AccPed_trqEngHiGear_MAP", 0x1C1448, 15, "Couple pédale Hi gear"       },
    { "AccPed_trqEngLoGear_MAP", 0x1C168C, 15, "Couple pédale Lo gear"       },
    { "FMTC_trq2qBas_MAP",       0x1C9AAA, 12, "Couple → Injection (FMTC)"   },
    { "Rail_pSetPointBase_MAP",  0x1E726C, 10, "Pression rail setpoint"            },
    { "EngPrt_trqAPSLim_MAP",    0x1C8838, 25, "Limite protection moteur"          },
};

static constexpr PopbangParams kEdc16c34_Popbang = {
    .nOvrRun = { 0x1C4046, 500,  5500, "RPM départ overrun"             },
    .qOvrRun = { 0x1C40B4, 0,    100,  "Qté carburant (brut ×0.1 mg)" },
};

static constexpr uint8_t kEdc16c34_DpfOff_Search[]  =
    { 0x7F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x01,0x01,0x00,0x0C,0x3B,0x0D,0x03 };
static constexpr uint8_t kEdc16c34_DpfOff_Replace[] =
    { 0x7F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x0C,0x3B,0x0D,0x03 };
static constexpr uint8_t kEdc16c34_DpfOff_Restore[] =
    { 0x7F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x01,0x01,0x00,0x0C,0x3B,0x0D,0x03 };

static constexpr AutoModPattern kEdc16c34_Patterns[] = {
    {
        "dpf_off",
        std::span<const uint8_t>{ kEdc16c34_DpfOff_Search,  sizeof kEdc16c34_DpfOff_Search  },
        std::span<const uint8_t>{ kEdc16c34_DpfOff_Replace, sizeof kEdc16c34_DpfOff_Replace },
        std::span<const uint8_t>{ kEdc16c34_DpfOff_Restore, sizeof kEdc16c34_DpfOff_Restore },
    },
};

static constexpr uint8_t kEdc16c34_EgrOff_Bytes[]     = { 0x1F, 0x40 };
static constexpr uint8_t kEdc16c34_DpfDtcOff_Bytes[]  = { 0xFF, 0xFF };
static constexpr uint8_t kEdc16c34_DpfDtcOff_Restore[]= { 0x00, 0x01 };

static constexpr AutoModAddress kEdc16c34_Addresses[] = {
    {
        "egr_off",
        0x1C41B8,
        std::span<const uint8_t>{ kEdc16c34_EgrOff_Bytes, sizeof kEdc16c34_EgrOff_Bytes },
        std::nullopt,
        "AirCtl_nMin_C → 8000 rpm (forum ecuedit/mhhauto)",
    },
    {
        "dpf_dtc_off",
        0x1E9DD4,
        std::span<const uint8_t>{ kEdc16c34_DpfDtcOff_Bytes,   sizeof kEdc16c34_DpfDtcOff_Bytes   },
        std::span<const uint8_t>{ kEdc16c34_DpfDtcOff_Restore, sizeof kEdc16c34_DpfDtcOff_Restore },
        "",
    },
};

// ── Catalog ──────────────────────────────────────────────────────────────────

static constexpr EcuEntry kCatalog[] = {
    // EDC16 ──────────────────────────────────────────────────────────────────
    {
        "edc16c34", "EDC16C34", "EDC16", "diesel",
        "PSA 1.6 HDi (DV6TED4 110cv / DV6BTED4 75ch) — 206 / 307 / 308 / Berlingo / Partner / C3 / C4",
        "ressources/edc16c34/damos.a2l",
        std::span<const Stage1Map>   { kEdc16c34_Stage1,    std::size(kEdc16c34_Stage1)    },
        kEdc16c34_Popbang,
        std::span<const AutoModPattern>{ kEdc16c34_Patterns,  std::size(kEdc16c34_Patterns)  },
        std::span<const AutoModAddress>{ kEdc16c34_Addresses, std::size(kEdc16c34_Addresses) },
    },
    {
        "edc16c39", "EDC16C39", "EDC16", "diesel",
        "PSA 2.0 HDi 136cv (DW10BTED4) — 407 / 607 / C5 / C6",
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    },
    {
        "edc16c3", "EDC16C3", "EDC16", "diesel",
        "VW / Audi / Seat / Skoda 1.9 TDI 105cv PD (BKC / BXE / BJB)",
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    },
    {
        "edc16u31", "EDC16U31", "EDC16", "diesel",
        "VW / Audi / Seat / Skoda 1.9 TDI 105cv CR (BLS)",
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    },
    {
        "edc16cp31", "EDC16CP31", "EDC16", "diesel",
        "BMW 318d / 320d / 520d (M47TU2)",
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    },
    {
        "edc16c2", "EDC16C2", "EDC16", "diesel",
        "Renault / Nissan 1.9 dCi 120cv (F9Q)",
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    },
    // EDC17 ──────────────────────────────────────────────────────────────────
    {
        "edc17c10", "EDC17C10", "EDC17", "diesel",
        "PSA 1.6 HDi 112cv BlueHDi (DV6C)",
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    },
    {
        "edc17c46", "EDC17C46", "EDC17", "diesel",
        "Renault 1.5 dCi 110cv (K9K) — Mégane / Clio / Kangoo",
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    },
    {
        "edc17c60", "EDC17C60", "EDC17", "diesel",
        "VW / Audi 2.0 TDI CR (EA288) — Golf 7 / A3 8V",
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    },
    // ME7 ────────────────────────────────────────────────────────────────────
    {
        "me7.4.4", "ME7.4.4", "ME7", "essence",
        "VW / Audi 1.8T 20v (AUM / ARZ / APX) — Golf 4 / A3 8L / TT",
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    },
    {
        "me7.5", "ME7.5", "ME7", "essence",
        "VW / Audi 1.8T / 2.0T (APY / AWU / BAM) — S3 / TT 225",
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    },
    // MED17 ──────────────────────────────────────────────────────────────────
    {
        "med17.5.25", "MED17.5.25", "MED17", "essence",
        "VW / Audi / Seat / Skoda 1.4 / 1.8 / 2.0 TFSI/TSI (EA111 / EA888)",
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    },
    {
        "med17.1", "MED17.1", "MED17", "essence",
        "BMW 2.0i / 3.0i (N43 / N53) — 1er / 3er / 5er",
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    },
};

// ── Public API ───────────────────────────────────────────────────────────────

std::span<const EcuEntry> catalog() {
    return kCatalog;
}

std::optional<EcuEntry> getEcu(std::string_view id) {
    for (const auto& e : kCatalog) {
        if (e.id == id) return e;
    }
    return std::nullopt;
}

std::vector<EcuSummary> listEcus() {
    std::vector<EcuSummary> out;
    out.reserve(std::size(kCatalog));
    for (const auto& e : kCatalog) {
        out.push_back({
            .id          = e.id,
            .name        = e.name,
            .family      = e.family,
            .fuel        = e.fuel,
            .application = e.application,
            .hasA2l      = e.a2l.has_value(),
            .hasStage1   = e.stage1Maps.has_value(),
            .hasPopbang  = e.popbangParams.has_value(),
        });
    }
    return out;
}

} // namespace ecu
