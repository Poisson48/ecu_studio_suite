#pragma once
//
// TuneValidation — compare les grandeurs OBD live à la consigne attendue
// issue des maps OpenDAMOS de la ROM flashée / en édition.
//
#include "ecu/OpenDamos.hpp"
#include "ecu/MapSampler.hpp"
#include "ecu/TunePackage.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ecu {

// Comment mapper l'axe Y d'une map quand l'unité OpenDAMOS est ambiguë.
enum class YAxisMode {
    OpenDamosAxis,   // utilise factor/offset de l'axe Y du recipe
    EngineLoadPct,   // PID 0x04 (charge moteur %) mappé sur la plage axe Y
};

// Comment dériver la grandeur mesurée pour une règle de validation.
enum class MeasureKind {
    MapAbsMbar,      // (MAP kPa + baro kPa) × 10 → mbar abs
    DirectPid,       // valeur brute du PID listé
};

struct ValidationRule {
    std::string   mapName;
    std::uint8_t  xPid       = 0x0C;   // RPM
    std::uint8_t  yPid       = 0x04;   // charge moteur
    MeasureKind   measure    = MeasureKind::MapAbsMbar;
    std::uint8_t  measurePid = 0x0B;   // MAP si MapAbsMbar
    YAxisMode     yMode      = YAxisMode::EngineLoadPct;
    std::string   category;            // boost, smoke, air, fuel, …
    bool          enabled    = true;
};

enum class ValidationStatus { Ok, Warn, Fail, NoData };

struct ValidationResult {
    QString           mapName;
    QString           category;
    QString           unit;
    double            measured     = 0.0;
    double            expected     = 0.0;
    double            delta        = 0.0;
    ValidationStatus  status       = ValidationStatus::NoData;
    double            xPhys        = 0.0;
    double            yPhys        = 0.0;
    int               ix0          = 0;
    int               iy0          = 0;
    std::size_t       mapAddress   = 0;
    QString           xUnit;
    QString           yUnit;
};

// Snapshot PID → valeur physique (mode 01 ou agrégat d'une passe de polling).
using LivePidSnapshot = std::unordered_map<std::uint8_t, double>;

struct SessionHitCell {
    int    gx = 0;
    int    gy = 0;
    int    count = 0;
    double sumAbsDelta = 0.0;
    double meanAbsDelta() const {
        return count > 0 ? sumAbsDelta / static_cast<double>(count) : 0.0;
    }
};

struct SessionSummary {
    int       ticks  = 0;
    int       ok     = 0;
    int       warn   = 0;
    int       fail   = 0;
    int       noData = 0;
    double    peakAbsDelta = 0.0;
    QString   peakMap;
    QString   csvPath;
    QString   romMd5;
    QString   ecuId;
    QDateTime started;
    QDateTime ended;
    std::vector<SessionHitCell> hotspots; // top cellules (abs delta moyen)

    double okRatio() const {
        const int n = ok + warn + fail;
        return n > 0 ? 100.0 * static_cast<double>(ok) / static_cast<double>(n) : 0.0;
    }
};

// Accumule stats + hotspots pendant une session conduite (pure, testable).
class SessionRecorder {
public:
    void reset();
    void start(const QString& ecuId, const QString& romMd5);
    void setCsvPath(const QString& path) { m_csvPath = path; }
    void ingest(const std::vector<ValidationResult>& results);
    SessionSummary finish();

    const SessionSummary& current() const { return m_sum; }
    bool active() const { return m_active; }

private:
    bool m_active = false;
    QString m_csvPath;
    SessionSummary m_sum;
    // clé = (gx << 16) | gy  — map primaire (1ʳᵉ règle non-NoData / boost)
    std::unordered_map<int, SessionHitCell> m_hits;
};

// Hystérésis d'affichage : Fail immédiat après N fails, OK seulement après N oks.
class StatusHysteresis {
public:
    void reset();
    void setFailThreshold(int n) { m_failNeed = n; }
    void setOkThreshold(int n)   { m_okNeed = n; }

    ValidationStatus update(ValidationStatus raw);
    int failStreak() const { return m_failStreak; }
    ValidationStatus displayed() const { return m_shown; }

private:
    int m_failNeed = 3;
    int m_okNeed   = 5;
    int m_failStreak = 0;
    int m_okStreak   = 0;
    ValidationStatus m_shown = ValidationStatus::NoData;
};

// EMA simple pour lisser le bandeau (alpha ∈ (0,1]).
class EmaFilter {
public:
    void reset() { m_init = false; m_v = 0.0; }
    void setAlpha(double a) { m_alpha = a; }
    double push(double v) {
        if (!m_init) { m_v = v; m_init = true; return m_v; }
        m_v = m_alpha * v + (1.0 - m_alpha) * m_v;
        return m_v;
    }
    double value() const { return m_v; }
    bool ready() const { return m_init; }

private:
    bool   m_init  = false;
    double m_alpha = 0.28;
    double m_v     = 0.0;
};

// Charge la ROM + recipe, relocalise les maps, et évalue les règles de validation.
class TuneValidator {
public:
    void clear();
    bool loadRom(const QByteArray& rom, const QString& ecuId);
    // Charge ROM + recipe JSON embarquée (package .ecutune) — sans filesystem recettes.
    bool loadRomWithRecipe(const QByteArray& rom, const QString& ecuId,
                           const QByteArray& recipeJsonUtf8);
    bool loadTunePackage(const TunePackage& pkg);

    bool isReady() const { return m_ready; }
    QString ecuId() const { return m_ecuId; }
    QString romMd5() const { return m_romMd5; }

    void setToleranceMbar(double v) { m_tolerance = v; }
    double toleranceMbar() const { return m_tolerance; }

    void setYAxisMode(YAxisMode m) { m_yMode = m; }
    YAxisMode yAxisMode() const { return m_yMode; }

    // Catégories OpenDAMOS retenues (défaut : boost, smoke, air, fuel).
    void setEnabledCategories(std::vector<std::string> cats);
    const std::vector<std::string>& enabledCategories() const { return m_categories; }

    void setRuleEnabled(const std::string& mapName, bool on);
    void setRules(std::vector<ValidationRule> rules); // remplace (après loadRom)

    const std::vector<ValidationRule>& rules() const { return m_rules; }

    // PIDs à poller pour toutes les règles actives (+ baro si MapAbsMbar).
    std::vector<std::uint8_t> requiredPids() const;

    std::optional<ValidationResult> evaluateRule(const ValidationRule& rule,
                                                 const LivePidSnapshot& live) const;

    std::vector<ValidationResult> evaluateAll(const LivePidSnapshot& live) const;

    std::optional<MapSampleResult> sampleMap(const std::string& mapName,
                                             double xPhys,
                                             double yPhys) const;

    std::optional<DamosEntry> entry(const std::string& name) const;
    std::optional<RelocResult> reloc(const std::string& name) const;

    // Helpers purs (tests / replay CSV).
    static ValidationStatus classifyDelta(double delta, double tolerance,
                                          const std::string& unit);
    static MeasureKind defaultMeasureForCategory(const std::string& cat);
    static std::uint8_t defaultMeasurePid(const std::string& cat);

private:
    void buildRules();
    std::optional<double> measuredValue(const ValidationRule& rule,
                                        const LivePidSnapshot& live) const;
    double yPhysForRule(const ValidationRule& rule,
                        const LivePidSnapshot& live,
                        const DamosEntry& entry) const;
    ValidationStatus classify(double delta, const std::string& unit) const;
    bool categoryEnabled(const std::string& cat) const;

    OpenDamos m_od;
    QByteArray m_rom;
    QString   m_ecuId;
    QString   m_romMd5;
    bool      m_ready = false;
    double    m_tolerance = 50.0;
    YAxisMode m_yMode = YAxisMode::EngineLoadPct;

    std::vector<std::string> m_categories{"boost", "smoke", "air", "fuel"};
    std::unordered_map<std::string, RelocResult> m_reloc;
    std::vector<ValidationRule> m_rules;
};

} // namespace ecu
