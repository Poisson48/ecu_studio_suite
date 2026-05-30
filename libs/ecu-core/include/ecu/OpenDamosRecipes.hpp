#pragma once
// Predefined auto-tune recipes that operate on relocated open_damos entries.
// Each recipe carries a list of typed operations applied to the ROM buffer.
//
// Operation semantics (mirrors the JS open-damos-recipes.js):
//   setPhys  – write a physical value as SWORD BE (raw = round((phys-offset)/factor))
//   setRaw   – write a SWORD BE directly
//   addPct   – multiply every cell of a MAP or CURVE by (1 + pct/100)
//   setMapAll– set every cell of a MAP or CURVE to the same physical value
//
// applyRecipe() modifies the ROM buffer in-place and returns a structured log.

#include "ecu/OpenDamos.hpp"
#include "ecu/RomPatcher.hpp"

#include <QByteArray>
#include <QList>
#include <QString>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ecu {

// ---------------------------------------------------------------------------
// Operation payload variants — exactly one field is active per Op.
// ---------------------------------------------------------------------------

struct OpSetPhys  { double phys; };
struct OpSetRaw   { int16_t raw; };
struct OpAddPct   { double pct; };
struct OpSetMapAll{ double phys; };

using OpPayload = std::variant<OpSetPhys, OpSetRaw, OpAddPct, OpSetMapAll>;

struct RecipeOp {
    std::string entry;   // open_damos entry name (e.g. "VSSCD_vMax_C")
    OpPayload   payload;
};

// ---------------------------------------------------------------------------
// Risk level.
// ---------------------------------------------------------------------------

enum class RecipeRisk { Low, Medium, High };

// ---------------------------------------------------------------------------
// A single recipe (mirrors the JS RECIPES object entries).
// ---------------------------------------------------------------------------

struct Recipe {
    std::string            id;
    std::string            name;
    std::string            category;
    std::string            description;
    RecipeRisk             risk;
    std::vector<RecipeOp>  ops;
};

// ---------------------------------------------------------------------------
// Summary returned by listRecipes() — no ops, just metadata.
// ---------------------------------------------------------------------------

struct RecipeSummary {
    std::string  id;
    std::string  name;
    std::string  category;
    std::string  description;
    RecipeRisk   risk;
    int          opsCount;
};

// ---------------------------------------------------------------------------
// Per-operation result logged by applyRecipe().
// ---------------------------------------------------------------------------

struct OpResult {
    std::string            entry;
    std::string            method;         // "setPhys"|"setRaw"|"addPct"|"setMapAll"|""
    std::optional<std::size_t>  address;
    std::string            addressSource;  // mirrors RelocResult::addressSource label
    std::optional<double>  physValue;
    std::optional<int16_t> rawValue;
    std::optional<int16_t> prevRaw;
    std::optional<int>     cellsChanged;
    std::optional<double>  pct;
    std::optional<std::string> error;      // present on failure; absent on success
};

// ---------------------------------------------------------------------------
// Return type of applyRecipe().
// ---------------------------------------------------------------------------

struct ApplyRecipeResult {
    bool               ok;            // true if at least one op succeeded
    std::vector<OpResult> operations;
    int                bytesChanged;
};

// ---------------------------------------------------------------------------
// Static data tables (hardcoded recipe library).
// ---------------------------------------------------------------------------

// All six predefined recipes as a compile-time-constructed table.
// Access via listRecipes() / getRecipe() / allRecipes().
const std::vector<Recipe>& allRecipes();

// ---------------------------------------------------------------------------
// Accessors.
// ---------------------------------------------------------------------------

// Returns a summary of every known recipe (id, name, category, description,
// risk, opsCount) — equivalent to JS listRecipes().
std::vector<RecipeSummary> listRecipes();

// Returns a pointer to the Recipe with the given id, or nullptr.
// Pointer is valid for the lifetime of the process (points into allRecipes()).
const Recipe* getRecipe(const std::string& id);

// ---------------------------------------------------------------------------
// applyPctToCurve — CURVE-specific percentage application.
//
// A CURVE has a 2-byte header (nx, UWordBE), then nx*2 axis bytes, then
// nx*2 data bytes.  applyPctToMap() (RomPatcher) expects a 4-byte MAP header,
// so this dedicated helper handles the CURVE layout.
//
// onlyPositive=true (default) mirrors the JS behaviour: skip cells ≤ 0.
// ---------------------------------------------------------------------------

struct ApplyPctCurveOptions {
    bool onlyPositive = true;
};

std::expected<std::vector<ChangedCell>, std::string>
applyPctToCurve(std::span<uint8_t> rom, std::size_t address, double pct,
                ApplyPctCurveOptions opts = {});

// ---------------------------------------------------------------------------
// applyRecipe — apply a recipe to a ROM buffer (modified in-place).
//
// Loads the open_damos for `ecu`, relocates every entry, then runs each op.
// Returns structured per-op results plus total bytes changed.
// Uses std::expected; never throws.
// ---------------------------------------------------------------------------

std::expected<ApplyRecipeResult, std::string>
applyRecipe(const Recipe& recipe, std::span<uint8_t> rom, const QString& ecu);

} // namespace ecu
