#pragma once
// Converts a DamosRecipe (+ optional pre-resolved relocation results from
// OpenDamos::relocate) into an ASAP2 A2L text file.  The output is valid
// ASAP2 1.60 and can be loaded by WinOLS, TunerPro, EcuFlash, open-car-reprog,
// and any ASAP2 1.6+ compliant tool.
//
// Two modes:
//   - baseline (no reloc results): uses DamosEntry::defaultAddress
//   - relocated (with RelocResult list): uses the resolved address so the A2L
//     matches a specific firmware
//
// The A2L includes RECORD_LAYOUTs, COMPU_METHODs, CHARACTERISTICs with axes,
// and a short PROJECT/MODULE header.

#include "ecu/OpenDamos.hpp"

#include <QString>

#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace ecu {

// ---------------------------------------------------------------------------
// Result of exportA2l().
// ---------------------------------------------------------------------------

struct A2lExportResult {
    QString                           a2l;        // full A2L text
    std::optional<std::vector<RelocResult>> relocation; // present when reloc was used
};

// ---------------------------------------------------------------------------
// OpenDamosA2lExport — stateless helper; all methods are static.
// ---------------------------------------------------------------------------

class OpenDamosA2lExport {
public:
    // Generate an A2L from a recipe using baseline (defaultAddress) addresses.
    // Returns an error string on failure (no exceptions).
    static std::expected<A2lExportResult, std::string>
    exportA2l(const DamosRecipe& recipe);

    // Generate an A2L from a recipe using pre-computed relocation results.
    // Entries absent from relocResults fall back to defaultAddress.
    static std::expected<A2lExportResult, std::string>
    exportA2l(const DamosRecipe& recipe,
              const std::vector<RelocResult>& relocResults);

    // ---------------------------------------------------------------------------
    // Helpers exposed for testing (mirrors the JS module-level functions).
    // ---------------------------------------------------------------------------

    // "0x" + 6 hex digits, uppercase — e.g. 0x01A2B3.
    static QString hexAddr(uint32_t addr);

    // Wrap a string in ASAP2 double quotes, escaping internal quotes.
    static QString quote(const QString& s);

    // Stable, dedup-safe COMPU_METHOD identifier for a (factor, offset, unit)
    // triple.  Mirrors compuMethodIdFor() in the JS source.
    static QString compuMethodId(double factor, double offset,
                                 const QString& unit);

    // Render a single COMPU_METHOD block (RAT_FUNC, linear).
    static QString renderCompuMethod(const QString& id, double factor,
                                     double offset, const QString& unit);

    // Render a RECORD_LAYOUT block for the given damos type string
    // ("MAP", "CURVE", "VALUE").  Returns nullopt for unknown types.
    static std::optional<QString> renderRecordLayout(const QString& name,
                                                     const QString& type);

    // Render a CHARACTERISTIC block (VALUE / CURVE / MAP) at the given address.
    static QString renderCharacteristic(const DamosEntry& entry, uint32_t addr);

private:
    // Indent every non-empty line of `s` by `n` spaces.
    static QString indent(const QString& s, int n = 2);
};

} // namespace ecu
