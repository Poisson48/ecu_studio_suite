#pragma once
//
// open_damos — a free (CC0) alternative to proprietary Bosch DAMOS for the
// EDC16C34 PSA family (and, later, other ECUs).
//
// Rationale: instead of hunting a proprietary damos for every firmware
// (different SW ID => different addresses), we use the RPM / pedal / torque
// axes as *unique fingerprints* to auto-relocate maps in any ROM of the same
// family. This lets a Stage 1 recipe apply to any EDC16C34 PSA firmware with
// no dedicated damos.
//
// Workflow:
//   1. OpenDamos::loadRecipe("edc16c34")  -> reads ressources/edc16c34/open_damos.json
//   2. OpenDamos::relocate(rom)           -> for each MAP/CURVE, scans the ROM
//      for the axis fingerprint; for each VALUE anchored on a MAP, applies the
//      same delta as the anchor (or a value-range search).
//   3. Returns the list of characteristics with a resolved address, an
//      addressSource in {Fingerprint, Anchor, DefaultFallback} and a
//      confidence in 0..1.
//
#include <QByteArray>
#include <QString>

#include <array>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ecu {

// ---------------------------------------------------------------------------
// Data model (mirrors the open_damos.json recipe schema).
// ---------------------------------------------------------------------------

enum class DamosDataType {
    SByte,
    UByte,
    SWordBE,
    UWordBE,
    SLongBE,
    ULongBE,
};

// Size in bytes of a DamosDataType.
std::size_t damosTypeSize(DamosDataType t);

// Parse a textual data type ("SWORD_BE", ...). Returns SWordBE for unknown.
DamosDataType parseDamosDataType(const std::string& s);

enum class DamosType { Map, Curve, Value, Unknown };

// One axis of a MAP/CURVE, with its expected fingerprint values.
struct DamosAxis {
    DamosDataType        dataType = DamosDataType::SWordBE;
    std::vector<int64_t> fingerprint;
    std::string          unit;     // physical unit (rpm, kg/h, %, …)
    std::string          quantity; // inputQuantity (Eng_nAvrg, …) — pour info
    double               factor = 1.0;
    double               offset = 0.0;
};

struct DamosDims {
    int nx = 0;
    int ny = 0;
};

struct DamosDataInfo {
    DamosDataType dataType = DamosDataType::SWordBE;
    double        factor   = 1.0;
    double        offset   = 0.0;
    std::string   unit;     // physical unit of cell values (Nm, mg/cyc, hPa, …)
};

// Relocation hints for VALUE characteristics.
struct DamosRelocation {
    std::optional<std::string>        anchorMap;
    std::optional<std::string>        method;       // e.g. "value-range-search"
    std::optional<std::array<double, 2>> valueRange; // physical [lo, hi]
    std::optional<std::size_t>        searchWindow; // bytes, full window
};

// A single characteristic entry of the recipe (MAP, CURVE or VALUE).
struct DamosEntry {
    std::string            name;
    DamosType              type = DamosType::Unknown;
    std::string            category;
    std::string            description;

    std::string            defaultAddress; // hex string or decimal
    DamosDims              dims;
    std::vector<DamosAxis> axes;
    DamosDataInfo          data;

    DamosRelocation        relocation;

    std::optional<int64_t> stockRawValue;
    std::optional<double>  stockPhysValue;

    // Free-form extras preserved for downstream consumers (stage1, egrOff...).
    bool                   hasStage1 = false;
    bool                   egrOff    = false;
};

// Auto-mod (patch) embarqué dans le recipe open_damos. Deux saveurs :
//   - "pattern" : remplace une séquence d'octets trouvée dans la ROM par une
//     autre. Optionnellement `restore` pour pouvoir annuler.
//   - "address" : écrit `bytes` à `address`. Optionnellement `restore`.
//
// Les chaînes d'octets sont stockées telles-quelles ("AA BB CC" ou "AABBCC")
// dans le JSON, et parsées en vecteur d'octets côté C++.
enum class DamosAutoModType { Unknown, Pattern, Address };

struct DamosAutoMod {
    std::string                 id;
    DamosAutoModType            type = DamosAutoModType::Unknown;
    std::string                 description;
    std::string                 note;

    // Pattern only.
    std::vector<uint8_t>        search;
    std::vector<uint8_t>        replace;
    std::vector<uint8_t>        restore;

    // Address only (`bytes` réutilise `replace` pour rester compact en mémoire).
    std::optional<std::uint64_t> address;
};

// The whole loaded recipe (the "damos").
struct DamosRecipe {
    std::string                ecuId;
    std::vector<DamosEntry>    characteristics;
    std::vector<DamosAutoMod>  autoMods;   // patches en un clic
};

// ---------------------------------------------------------------------------
// Tuning knobs for the fuzzy axis matcher and the ROM scan.
// ---------------------------------------------------------------------------

struct AxisTolerance {
    double absTol        = 100.0;
    double relTol        = 0.05;
    double strictMinFrac = 0.85;
    double bagMinFrac    = 0.70;
};

struct RelocateOptions {
    std::size_t                step = 2;
    std::optional<std::size_t> startOffset;
    std::optional<std::size_t> endOffset;
    AxisTolerance              axisTol;
};

// ---------------------------------------------------------------------------
// Results.
// ---------------------------------------------------------------------------

enum class AxisMatchMode { None, Strict, Bag };

struct AxisMatchResult {
    bool          match = false;
    double        score = 0.0;
    AxisMatchMode mode  = AxisMatchMode::None;
    int           matches = 0;
    int           total   = 0;
};

// One candidate location for a MAP/CURVE fingerprint in the ROM.
struct FingerprintCandidate {
    std::size_t          address = 0;
    double               score   = 0.0;
    AxisMatchMode        xMode   = AxisMatchMode::None;
    AxisMatchMode        yMode   = AxisMatchMode::None;
    std::vector<int64_t> xAxis;
    std::vector<int64_t> yAxis;
};

// Resolution of a VALUE anchored on a found MAP.
struct ValueMatch {
    std::size_t            address    = 0;
    std::int64_t           delta      = 0;
    std::optional<int64_t> raw;
    std::optional<double>  physValue;
    double                 confidence = 0.0;
    bool                   plausible  = false;
    std::string            method;     // "anchor-delta" | "value-range-search"
};

enum class AddressSource { Fingerprint, Anchor, DefaultFallback };

// Per-characteristic relocation outcome.
struct RelocResult {
    std::string            name;
    DamosType              type = DamosType::Unknown;
    std::string            category;
    std::string            description;

    std::size_t            address        = 0;
    std::size_t            defaultAddress = 0;
    std::int64_t           delta          = 0;
    AddressSource          addressSource  = AddressSource::DefaultFallback;
    std::string            matchMode;        // e.g. "strict/bag" for MAP
    double                 score          = 0.0;

    std::optional<std::string> anchorMap;
    std::optional<int64_t>     raw;
    std::optional<double>      physValue;
    std::optional<std::string> warning;

    DamosType              entryType() const { return type; }
};

// ---------------------------------------------------------------------------
// OpenDamos — load a recipe, fingerprint a ROM, relocate every characteristic.
// ---------------------------------------------------------------------------

class OpenDamos {
public:
    // Load a recipe from ressources/<ecu>/open_damos.json. baseDir overrides
    // the resource root (default: "ressources"). Returns an error message on
    // failure (no exceptions are thrown).
    static std::expected<DamosRecipe, std::string>
    loadRecipe(const QString& ecu, const QString& baseDir = {});

    // Load a recipe directly from a JSON document (string).
    static std::expected<DamosRecipe, std::string>
    parseRecipe(const std::string& json);

    // Serialize a recipe back to its open_damos.json representation. Re-parsable
    // by parseRecipe() — used by the in-app DAMOS editor.
    static std::string serializeRecipe(const DamosRecipe& recipe);

    // Write a recipe to disk (creates parent directories as needed).
    static std::expected<void, std::string>
    saveRecipe(const DamosRecipe& recipe, const QString& path);

    // Scan the whole ROM for the axis fingerprint of one MAP/CURVE entry.
    // Returns all candidates, sorted best-first.
    static std::vector<FingerprintCandidate>
    findMapByFingerprint(QByteArrayView rom, const DamosEntry& entry,
                         const RelocateOptions& opts = {});

    // Resolve a VALUE entry by anchoring on an already-found MAP candidate.
    static std::optional<ValueMatch>
    findValueByAnchor(QByteArrayView rom, const DamosEntry& entry,
                      const DamosEntry& anchorEntry, std::size_t anchorAddress);

    // Full relocation of every characteristic of the recipe against a ROM.
    std::vector<RelocResult>
    relocate(const DamosRecipe& recipe, QByteArrayView rom,
             const RelocateOptions& opts = {}) const;

    // Convenience: relocate using a previously loaded recipe.
    std::vector<RelocResult>
    relocate(QByteArrayView rom, const RelocateOptions& opts = {}) const;

    // Set the recipe used by the convenience relocate() overload.
    void setRecipe(DamosRecipe recipe) { m_recipe = std::move(recipe); }
    const std::optional<DamosRecipe>& recipe() const { return m_recipe; }

private:
    std::optional<DamosRecipe> m_recipe;
};

} // namespace ecu
