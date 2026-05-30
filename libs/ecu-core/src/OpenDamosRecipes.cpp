#include "ecu/OpenDamosRecipes.hpp"
#include "ecu/OpenDamos.hpp"
#include "ecu/RomPatcher.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <span>
#include <string_view>
#include <unordered_map>

namespace ecu {

// ---------------------------------------------------------------------------
// Hardcoded recipe table.
// ---------------------------------------------------------------------------

// Risk helper used in the table initialiser.
static constexpr RecipeRisk Low    = RecipeRisk::Low;
static constexpr RecipeRisk Medium = RecipeRisk::Medium;

const std::vector<Recipe>& allRecipes() {
    // Constructed once; the static guarantees thread-safe initialisation in C++11+.
    static const std::vector<Recipe> kRecipes = {
        // UTF-8 multi-byte sequences use octal (3 digits, unambiguous boundary).
        // \303\240 = 0xC3 0xA0 = à    \303\250 = 0xC3 0xA8 = è
        // \303\251 = 0xC3 0xA9 = é    \342\200\224 = 0xE2 0x80 0x94 = —
        // \342\206\222 = 0xE2 0x86 0x92 = →
        {
            .id          = "speed_limiter_off",
            .name        = "Speed Limiter OFF \342\200\224 tous plafonds vitesse \303\240 320 km/h",
            .category    = "Limiters",
            .description = "Rel\303\250ve tous les plafonds de vitesse connus (r\303\251gulateur, "
                           "diagnostic, propulsion) \303\240 320 km/h. Au-del\303\240, le v\303\251hicule "
                           "n'est plus brid\303\251 \303\251lectroniquement.",
            .risk = Low,
            .ops  = {
                { "VSSCD_vMax_C",        OpSetPhys{320} },
                { "CrCCD_vSetSpdMax_C",  OpSetPhys{320} },
                { "PrpCCD_vSetSpdMax_C", OpSetPhys{320} },
            },
        },
        {
            .id          = "smoke_off",
            .name        = "Smoke limiter assoupli \342\200\224 permet +20% de fuel sans coupure",
            .category    = "Performance",
            .description = "Baisse la lambda mini du smoke cut (FlMng_rLmbdSmk_MAP) de 5% globalement "
                           "\342\206\222 plus de fuel autoris\303\251 avant que le limiteur de fum\303\251"
                           "e ne tire. Utile pour Stage 1+ si tu vois de la fum\303\251e noire pleine charge.",
            .risk = Medium,
            .ops  = {
                { "FlMng_rLmbdSmk_MAP", OpAddPct{-5} },
            },
        },
        {
            .id          = "torque_limiter_off",
            .name        = "Torque Limiter OFF \342\200\224 rel\303\250ve les plafonds protection",
            .category    = "Limiters",
            .description = "Rel\303\250ve les 2 plafonds couple (EngPrt_trqAPSLim_MAP + "
                           "EngPrt_qLim_CUR) de 30%. Ces plafonds clamment tes gains Stage 1/2, "
                           "les monter \303\251vite les saturations.",
            .risk = Medium,
            .ops  = {
                { "EngPrt_trqAPSLim_MAP", OpAddPct{30} },
                { "EngPrt_qLim_CUR",      OpAddPct{25} },
            },
        },
        {
            .id          = "rev_limit_raise",
            .name        = "Rev Limiter \342\200\224 zone non-monitored relev\303\251e",
            .category    = "Limiters",
            .description = "Rel\303\250ve AccPed_nLimNMR_C (seuil r\303\251gime non-monitored) "
                           "de 1500 \303\240 5500 rpm. Permet plus de souplesse aux hauts r\303\251gimes "
                           "sans trigger les diag.",
            .risk = Low,
            .ops  = {
                { "AccPed_nLimNMR_C", OpSetPhys{5500} },
            },
        },
        {
            .id          = "rail_max_raise",
            .name        = "Rail Pressure Max \342\200\224 plafond \303\240 1800 bar",
            .category    = "Performance",
            .description = "Rel\303\250ve le plafond de pression rail (Rail_pSetPointMax_MAP) de 15% "
                           "\342\206\222 ~1800 bar. N\303\251cessaire pour Stage 2+ quand "
                           "Rail_pSetPointBase atteint ce plafond.",
            .risk = Medium,
            .ops  = {
                { "Rail_pSetPointMax_MAP", OpAddPct{15} },
            },
        },
        {
            .id          = "full_depollution",
            .name        = "D\303\251pollution compl\303\250te \342\200\224 EGR OFF + seuils relev\303\251s",
            .category    = "D\303\251pollution",
            .description = "Coupe EGR d\303\251finitivement (seuil 8000 rpm via AirCtl_nMin_C) + "
                           "rel\303\250ve AccPed_trqNMRMax_C pour \303\251viter les clamp au Stage 1. "
                           "ATTENTION : combiner avec un d\303\251fap m\303\251canique.",
            .risk = Low,
            .ops  = {
                { "AirCtl_nMin_C",      OpSetPhys{8000} },
                { "AccPed_trqNMRMax_C", OpSetPhys{250}  },
            },
        },
    };
    return kRecipes;
}

// ---------------------------------------------------------------------------
// Accessors.
// ---------------------------------------------------------------------------

std::vector<RecipeSummary> listRecipes() {
    const auto& recipes = allRecipes();
    std::vector<RecipeSummary> out;
    out.reserve(recipes.size());
    for (const Recipe& r : recipes) {
        out.push_back({
            .id          = r.id,
            .name        = r.name,
            .category    = r.category,
            .description = r.description,
            .risk        = r.risk,
            .opsCount    = static_cast<int>(r.ops.size()),
        });
    }
    return out;
}

const Recipe* getRecipe(const std::string& id) {
    for (const Recipe& r : allRecipes()) {
        if (r.id == id)
            return &r;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// applyPctToCurve.
//
// CURVE memory layout (all big-endian):
//   [address+0]  nx  (uint16, 2 bytes)          — number of axis/data cells
//   [address+2]  x-axis: nx SWORD_BE values     — nx*2 bytes
//   [address+2+nx*2]  data: nx SWORD_BE values  — nx*2 bytes
//
// The 2-byte header distinguishes this from MAP (4-byte header: nx, ny).
// ---------------------------------------------------------------------------

std::expected<std::vector<ChangedCell>, std::string>
applyPctToCurve(std::span<uint8_t> rom, std::size_t address, double pct,
                ApplyPctCurveOptions opts) {
    if (address + 2 > rom.size())
        return std::unexpected(
            std::format("applyPctToCurve: address 0x{:X} out of ROM bounds", address));

    const int nx = (static_cast<int>(rom[address]) << 8) | static_cast<int>(rom[address + 1]);
    if (nx < 2 || nx > 64)
        return std::unexpected(
            std::format("Invalid curve dim nx={} at 0x{:X}", nx, address));

    // Skip 2-byte header + nx axis SWORD_BEs to reach the data section.
    const std::size_t dataOff = address + 2 + static_cast<std::size_t>(nx) * 2;
    const std::size_t dataEnd = dataOff + static_cast<std::size_t>(nx) * 2;
    if (dataEnd > rom.size())
        return std::unexpected(
            std::format("Curve at 0x{:X} extends past ROM end", address));

    const double factor = 1.0 + pct / 100.0;
    std::vector<ChangedCell> changed;

    for (int i = 0; i < nx; ++i) {
        const std::size_t cellOff = dataOff + static_cast<std::size_t>(i) * 2;
        const int16_t raw = readSwordBE(rom, cellOff);

        if (opts.onlyPositive && raw <= 0)
            continue;

        const double scaled = std::clamp(
            std::round(static_cast<double>(raw) * factor),
            static_cast<double>(std::numeric_limits<int16_t>::min()),
            static_cast<double>(std::numeric_limits<int16_t>::max()));
        const auto newRaw = static_cast<int16_t>(scaled);

        if (newRaw != raw) {
            writeSwordBE(rom, cellOff, static_cast<double>(newRaw));
            changed.push_back({cellOff, raw, newRaw});
        }
    }

    return changed;
}

// ---------------------------------------------------------------------------
// Internal helper — convert AddressSource enum to string for OpResult.
// ---------------------------------------------------------------------------

static std::string addressSourceLabel(AddressSource src) {
    switch (src) {
        case AddressSource::Fingerprint:    return "fingerprint";
        case AddressSource::Anchor:         return "anchor";
        case AddressSource::DefaultFallback:return "default-fallback";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// applyRecipe.
// ---------------------------------------------------------------------------

std::expected<ApplyRecipeResult, std::string>
applyRecipe(const Recipe& recipe, std::span<uint8_t> rom, const QString& ecu) {
    // Load and relocate open_damos entries for this ECU.
    auto damosResult = OpenDamos::loadRecipe(ecu);
    if (!damosResult)
        return std::unexpected(
            std::format("No open_damos for ECU {}: {}", ecu.toStdString(), damosResult.error()));

    OpenDamos od;
    od.setRecipe(std::move(*damosResult));

    const QByteArray romView(reinterpret_cast<const char*>(rom.data()),
                             static_cast<qsizetype>(rom.size()));
    const std::vector<RelocResult> relocated = od.relocate(romView);

    // Build name → RelocResult lookup.
    std::unordered_map<std::string, const RelocResult*> byName;
    byName.reserve(relocated.size());
    for (const RelocResult& r : relocated)
        byName.emplace(r.name, &r);

    ApplyRecipeResult result;
    result.ok           = false;
    result.bytesChanged = 0;

    for (const RecipeOp& op : recipe.ops) {
        OpResult opRes;
        opRes.entry = op.entry;

        auto it = byName.find(op.entry);
        if (it == byName.end()) {
            opRes.method = "";
            opRes.error  = "Entry not found in open_damos";
            result.operations.push_back(std::move(opRes));
            continue;
        }

        const RelocResult& rel = *it->second;

        // Skip entries that could not be reliably relocated — same safety gate
        // as the JS: addressSource == default-fallback OR score == 0.
        if (rel.addressSource == AddressSource::DefaultFallback || rel.score == 0.0) {
            opRes.address       = rel.address;
            opRes.addressSource = addressSourceLabel(rel.addressSource);
            opRes.error         = std::format(
                "Entry could not be relocated on this ROM ({}), skipping for safety",
                rel.warning.value_or("no fingerprint"));
            result.operations.push_back(std::move(opRes));
            continue;
        }

        opRes.address       = rel.address;
        opRes.addressSource = addressSourceLabel(rel.addressSource);

        // Dispatch on the op payload type and entry type.
        std::visit([&](const auto& payload) {
            using P = std::decay_t<decltype(payload)>;

            if constexpr (std::is_same_v<P, OpSetPhys>) {
                if (rel.type != DamosType::Value) {
                    opRes.error  = std::format("Unsupported operation setPhys on type {}",
                                               static_cast<int>(rel.type));
                    opRes.method = "setPhys";
                    return;
                }
                // Retrieve scaling from the DamosRecipe if available; the
                // RelocResult itself doesn't cache factor/offset, so we look
                // it up from the loaded recipe characteristics.
                double factor = 1.0;
                double offset = 0.0;
                if (od.recipe()) {
                    for (const DamosEntry& e : od.recipe()->characteristics) {
                        if (e.name == op.entry) {
                            factor = e.data.factor;
                            offset = e.data.offset;
                            break;
                        }
                    }
                }
                const double rawD = std::round((payload.phys - offset) / factor);
                const auto clamped = static_cast<int16_t>(
                    std::clamp(rawD,
                               static_cast<double>(std::numeric_limits<int16_t>::min()),
                               static_cast<double>(std::numeric_limits<int16_t>::max())));

                const int16_t prev = readSwordBE(rom, rel.address);
                auto wRes = writeValue(rom, rel.address, static_cast<double>(clamped));
                if (!wRes) {
                    opRes.error  = wRes.error();
                    opRes.method = "setPhys";
                    return;
                }
                if (prev != clamped)
                    result.bytesChanged += 2;
                opRes.method    = "setPhys";
                opRes.physValue = payload.phys;
                opRes.rawValue  = clamped;
                opRes.prevRaw   = prev;

            } else if constexpr (std::is_same_v<P, OpSetRaw>) {
                if (rel.type != DamosType::Value) {
                    opRes.error  = std::format("Unsupported operation setRaw on type {}",
                                               static_cast<int>(rel.type));
                    opRes.method = "setRaw";
                    return;
                }
                const int16_t prev = readSwordBE(rom, rel.address);
                auto wRes = writeValue(rom, rel.address, static_cast<double>(payload.raw));
                if (!wRes) {
                    opRes.error  = wRes.error();
                    opRes.method = "setRaw";
                    return;
                }
                if (prev != payload.raw)
                    result.bytesChanged += 2;
                opRes.method   = "setRaw";
                opRes.rawValue = payload.raw;
                opRes.prevRaw  = prev;

            } else if constexpr (std::is_same_v<P, OpAddPct>) {
                if (rel.type == DamosType::Map) {
                    auto chRes = applyPctToMap(rom, rel.address, payload.pct);
                    if (!chRes) {
                        opRes.error  = chRes.error();
                        opRes.method = "addPct";
                        return;
                    }
                    result.bytesChanged += static_cast<int>(chRes->size()) * 2;
                    opRes.method       = "addPct";
                    opRes.pct          = payload.pct;
                    opRes.cellsChanged = static_cast<int>(chRes->size());

                } else if (rel.type == DamosType::Curve) {
                    auto chRes = applyPctToCurve(rom, rel.address, payload.pct);
                    if (!chRes) {
                        opRes.error  = chRes.error();
                        opRes.method = "addPct";
                        return;
                    }
                    result.bytesChanged += static_cast<int>(chRes->size()) * 2;
                    opRes.method       = "addPct";
                    opRes.pct          = payload.pct;
                    opRes.cellsChanged = static_cast<int>(chRes->size());

                } else {
                    opRes.error  = std::format("Unsupported operation addPct on type {}",
                                               static_cast<int>(rel.type));
                    opRes.method = "addPct";
                }

            } else if constexpr (std::is_same_v<P, OpSetMapAll>) {
                if (rel.type != DamosType::Map && rel.type != DamosType::Curve) {
                    opRes.error  = std::format("Unsupported operation setMapAll on type {}",
                                               static_cast<int>(rel.type));
                    opRes.method = "setMapAll";
                    return;
                }
                double factor = 1.0;
                double offset = 0.0;
                if (od.recipe()) {
                    for (const DamosEntry& e : od.recipe()->characteristics) {
                        if (e.name == op.entry) {
                            factor = e.data.factor;
                            offset = e.data.offset;
                            break;
                        }
                    }
                }
                const double rawD = std::round((payload.phys - offset) / factor);
                const auto raw = static_cast<int16_t>(
                    std::clamp(rawD,
                               static_cast<double>(std::numeric_limits<int16_t>::min()),
                               static_cast<double>(std::numeric_limits<int16_t>::max())));

                // readMapData works for both MAP and CURVE because they share
                // a compatible header subset; the MAP reader reads nx/ny from
                // 4 bytes, while a CURVE only has nx.  For CURVE we handle the
                // cell iteration manually to avoid the ny read-overrun.
                int cells = 0;
                if (rel.type == DamosType::Map) {
                    auto mapRes = readMapData(rom, rel.address);
                    if (!mapRes) {
                        opRes.error  = mapRes.error();
                        opRes.method = "setMapAll";
                        return;
                    }
                    const std::size_t cellCount = mapRes->data.size();
                    for (std::size_t i = 0; i < cellCount; ++i) {
                        const std::size_t cellOff = mapRes->dataOff + i * 2;
                        const int16_t prev = readSwordBE(rom, cellOff);
                        writeValue(rom, cellOff, static_cast<double>(raw));
                        if (prev != raw) { ++cells; result.bytesChanged += 2; }
                    }
                } else {
                    // CURVE: 2-byte header (nx), nx axis SWORDs, nx data SWORDs.
                    if (rel.address + 2 > rom.size()) {
                        opRes.error  = std::format("setMapAll: address 0x{:X} out of ROM bounds",
                                                   rel.address);
                        opRes.method = "setMapAll";
                        return;
                    }
                    const int nx = (static_cast<int>(rom[rel.address]) << 8)
                                 | static_cast<int>(rom[rel.address + 1]);
                    if (nx < 2 || nx > 64) {
                        opRes.error  = std::format("setMapAll: invalid curve dim nx={} at 0x{:X}",
                                                   nx, rel.address);
                        opRes.method = "setMapAll";
                        return;
                    }
                    const std::size_t dataOff = rel.address + 2 + static_cast<std::size_t>(nx) * 2;
                    for (int i = 0; i < nx; ++i) {
                        const std::size_t cellOff = dataOff + static_cast<std::size_t>(i) * 2;
                        const int16_t prev = readSwordBE(rom, cellOff);
                        writeValue(rom, cellOff, static_cast<double>(raw));
                        if (prev != raw) { ++cells; result.bytesChanged += 2; }
                    }
                }
                opRes.method       = "setMapAll";
                opRes.physValue    = payload.phys;
                opRes.rawValue     = raw;
                opRes.cellsChanged = cells;
            }
        }, op.payload);

        result.operations.push_back(std::move(opRes));
    }

    result.ok = std::ranges::any_of(result.operations,
                                    [](const OpResult& o) { return !o.error.has_value(); });
    return result;
}

} // namespace ecu
