#include "ecu/VehicleTemplates.hpp"

#include <algorithm>
#include <ranges>

namespace ecu {

const std::vector<VehicleTemplate> VEHICLE_TEMPLATES = {

    // ── DV6BTED4 55kW 75ch ───────────────────────────────────────────────────
    {
        .id          = "psa_16hdi_75_stage1_safe",
        .name        = "PSA 1.6 HDi 75ch — Stage 1 Safe",
        .description = "Stage 1 conservateur adapté au DV6BTED4 75ch (≈+8ch / +15Nm estimé). "
                       "Reste dans les limites du turbo et de l'embrayage d'origine.",
        .vehicles    = "Berlingo I / Partner / 206 / 307 / C3 1.6 HDi 75cv (DV6BTED4)",
        .appliesTo   = {"edc16c34"},
        .appliesToVariant = std::vector<std::string>{"75ch"},
        .stage1      = Stage1{{ {"AccPed_trqEngHiGear_MAP", 6},
                                {"AccPed_trqEngLoGear_MAP", 6},
                                {"FMTC_trq2qBas_MAP",       5},
                                {"Rail_pSetPointBase_MAP",  4},
                                {"EngPrt_trqAPSLim_MAP",    6} }},
        .popbang     = std::nullopt,
        .autoMods    = {},
    },
    {
        .id          = "psa_16hdi_75_stage1_sport",
        .name        = "PSA 1.6 HDi 75ch — Stage 1 Sport",
        .description = "Stage 1 au maximum recommandé pour le 75ch (≈+12ch / +20Nm estimé). "
                       "Limite moteur relevée à +8%. Turbo et embrayage d'origine requis en bon état.",
        .vehicles    = "Berlingo I / Partner / 206 / 307 / C3 1.6 HDi 75cv (DV6BTED4)",
        .appliesTo   = {"edc16c34"},
        .appliesToVariant = std::vector<std::string>{"75ch"},
        .stage1      = Stage1{{ {"AccPed_trqEngHiGear_MAP", 8},
                                {"AccPed_trqEngLoGear_MAP", 8},
                                {"FMTC_trq2qBas_MAP",       7},
                                {"Rail_pSetPointBase_MAP",  6},
                                {"EngPrt_trqAPSLim_MAP",    8} }},
        .popbang     = std::nullopt,
        .autoMods    = {},
    },

    // ── DV6TED4 66kW 90ch ────────────────────────────────────────────────────
    {
        .id          = "psa_16hdi_90_stage1_safe",
        .name        = "PSA 1.6 HDi 90ch — Stage 1 Safe",
        .description = "Stage 1 conservateur pour le DV6TED4 90ch (≈+12ch / +25Nm estimé).",
        .vehicles    = "207 / 308 / 3008 / C3 / C4 1.6 HDi 90cv (DV6TED4)",
        .appliesTo   = {"edc16c34"},
        .appliesToVariant = std::vector<std::string>{"90ch"},
        .stage1      = Stage1{{ {"AccPed_trqEngHiGear_MAP", 8},
                                {"AccPed_trqEngLoGear_MAP", 8},
                                {"FMTC_trq2qBas_MAP",       7},
                                {"Rail_pSetPointBase_MAP",  5},
                                {"EngPrt_trqAPSLim_MAP",   10} }},
        .popbang     = std::nullopt,
        .autoMods    = {},
    },
    {
        .id          = "psa_16hdi_90_stage1_sport",
        .name        = "PSA 1.6 HDi 90ch — Stage 1 Sport",
        .description = "Stage 1 agressif pour le 90ch (≈+18ch / +35Nm estimé). "
                       "À réserver à un turbo sain.",
        .vehicles    = "207 / 308 / 3008 / C3 / C4 1.6 HDi 90cv (DV6TED4)",
        .appliesTo   = {"edc16c34"},
        .appliesToVariant = std::vector<std::string>{"90ch"},
        .stage1      = Stage1{{ {"AccPed_trqEngHiGear_MAP", 12},
                                {"AccPed_trqEngLoGear_MAP", 12},
                                {"FMTC_trq2qBas_MAP",       10},
                                {"Rail_pSetPointBase_MAP",   8},
                                {"EngPrt_trqAPSLim_MAP",    12} }},
        .popbang     = std::nullopt,
        .autoMods    = {},
    },

    // ── DV6TED4 81kW 110ch ───────────────────────────────────────────────────
    {
        .id          = "psa_16hdi_110_stage1_safe",
        .name        = "PSA 1.6 HDi 110ch — Stage 1 Safe",
        .description = "Stage 1 conservateur (≈+20ch / +40Nm estimé). "
                       "Garde une marge sur rail et protection moteur. Turbo et embrayage d'origine OK.",
        .vehicles    = "206 / 307 / 308 / Berlingo II / C3 / C4 HDi 110cv (DV6TED4)",
        .appliesTo   = {"edc16c34"},
        .appliesToVariant = std::vector<std::string>{"110ch"},
        .stage1      = Stage1{{ {"AccPed_trqEngHiGear_MAP", 10},
                                {"AccPed_trqEngLoGear_MAP", 10},
                                {"FMTC_trq2qBas_MAP",        8},
                                {"Rail_pSetPointBase_MAP",   6},
                                {"EngPrt_trqAPSLim_MAP",    15} }},
        .popbang     = std::nullopt,
        .autoMods    = {},
    },
    {
        .id          = "psa_16hdi_110_stage1_sport",
        .name        = "PSA 1.6 HDi 110ch — Stage 1 Sport + Pop&Bang",
        .description = "Stage 1 agressif (≈+30ch / +60Nm estimé) avec Pop & Bang overrun léger "
                       "(CT compatible). À réserver à un turbo + embrayage sains.",
        .vehicles    = "206 / 307 / 308 / Berlingo II / C3 / C4 HDi 110cv (DV6TED4)",
        .appliesTo   = {"edc16c34"},
        .appliesToVariant = std::vector<std::string>{"110ch"},
        .stage1      = Stage1{{ {"AccPed_trqEngHiGear_MAP", 15},
                                {"AccPed_trqEngLoGear_MAP", 15},
                                {"FMTC_trq2qBas_MAP",       12},
                                {"Rail_pSetPointBase_MAP",  10},
                                {"EngPrt_trqAPSLim_MAP",    25} }},
        .popbang     = PopBang{.rpm = 3000, .fuelQty = 12},
        .autoMods    = {},
    },

    // ── Dépollution — toutes variantes DV6 ────────────────────────────────────
    {
        .id          = "psa_16hdi_depollution_off",
        .name        = "PSA 1.6 HDi — Dépollution OFF (EGR + FAP + DTC)",
        .description = "FAP, DTC FAP, EGR désactivés. "
                       "⚠ À faire après dépose physique (FAP vidé, EGR bouchée). "
                       "Repasser stock avant CT.",
        .vehicles    = "Toutes variantes DV6 75ch / 90ch / 110ch (EDC16C34)",
        .appliesTo   = {"edc16c34"},
        .appliesToVariant = std::nullopt,
        .stage1      = std::nullopt,
        .popbang     = std::nullopt,
        .autoMods    = {"dpf_off", "dpf_dtc_off", "egr_off"},
    },
};

// ── helpers ──────────────────────────────────────────────────────────────────

static TemplateSummary toSummary(const VehicleTemplate& t) {
    return {
        .id               = t.id,
        .name             = t.name,
        .description      = t.description,
        .vehicles         = t.vehicles,
        .appliesTo        = t.appliesTo,
        .appliesToVariant = t.appliesToVariant,
        .hasStage1        = t.stage1.has_value(),
        .hasPopbang       = t.popbang.has_value(),
        .autoModCount     = t.autoMods.size(),
    };
}

// ── public API ───────────────────────────────────────────────────────────────

std::vector<TemplateSummary> listTemplates() {
    std::vector<TemplateSummary> out;
    out.reserve(VEHICLE_TEMPLATES.size());
    for (const auto& t : VEHICLE_TEMPLATES)
        out.push_back(toSummary(t));
    return out;
}

std::optional<VehicleTemplate> getTemplate(const std::string& id) {
    auto it = std::ranges::find_if(VEHICLE_TEMPLATES,
        [&id](const VehicleTemplate& t) { return t.id == id; });
    if (it == VEHICLE_TEMPLATES.end())
        return std::nullopt;
    return *it;
}

std::vector<TemplateSummary> listTemplatesForEcu(const std::string& ecuId) {
    std::vector<TemplateSummary> out;
    for (const auto& t : VEHICLE_TEMPLATES) {
        if (std::ranges::find(t.appliesTo, ecuId) != t.appliesTo.end())
            out.push_back(toSummary(t));
    }
    return out;
}

std::vector<TemplateSummary> listTemplatesForVariant(const std::string& ecuId,
                                                      const std::string& variant) {
    auto byEcu = listTemplatesForEcu(ecuId);
    std::vector<TemplateSummary> out;
    for (const auto& s : byEcu) {
        // appliesToVariant == nullopt means the template applies to all variants
        if (!s.appliesToVariant.has_value() ||
            std::ranges::find(*s.appliesToVariant, variant) != s.appliesToVariant->end())
            out.push_back(s);
    }
    return out;
}

} // namespace ecu
