#pragma once
#include "ecu/MapDiffer.hpp"
#include <QByteArray>
#include <QList>
#include <QString>
#include <optional>
#include <span>

namespace ecu {

// Metadata about the project being reported on. Mirrors the relevant fields
// from JS generateReport()'s `project` argument plus the git context args.
struct ReportProject {
    QString name;
    QString vehicle;
    QString immat;
    QString year;
    QString ecu;        // will be uppercased in the report
    QString romName;
    QString branchName;
    QString headHash;   // full hash; report shows first 12 chars
};

// All information needed to build one HTML tune report.
struct ReportInput {
    ReportProject               project;
    std::span<const uint8_t>   originalBuf;
    std::span<const uint8_t>   currentBuf;
    // Characteristics from A2L; may be empty (report will still be produced).
    std::span<const DiffCharacteristic> characteristics;
};

class ReportGenerator {
public:
    ReportGenerator() = default;

    // Generates a self-contained HTML report string (UTF-8, inline CSS, print
    // button) ready to be written to a file or displayed in a QWebEngineView.
    // Never throws; returns a null QString only if input data is fundamentally
    // unusable (currently always succeeds).
    [[nodiscard]] std::optional<QString> generate(const ReportInput& input) const;

private:
    // Escapes the five XML/HTML special characters for safe embedding in HTML.
    static QString escapeHtml(const QString& s);

    // Formats an optional AvgChange as a signed percentage string (e.g. "+12 %").
    // Returns an empty string when avg is absent or not finite.
    static QString formatPct(const std::optional<AvgChange>& avg);

    // Builds the CSS block (kept as a separate helper to avoid cluttering
    // generate() with a multi-line raw string literal).
    static QString buildCss();

    // Builds the <tbody> rows for the maps table.
    static QString buildMapRows(const std::vector<MapDiffResult>& maps);

    // Builds the two-column meta-information table rows.
    static QString buildMetaRows(const ReportProject& project,
                                 const QString& generatedAt);
};

} // namespace ecu
