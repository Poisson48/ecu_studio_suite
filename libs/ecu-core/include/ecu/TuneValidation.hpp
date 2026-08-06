#pragma once
//
// TuneValidation — compare les grandeurs OBD live à la consigne attendue
// issue des maps OpenDAMOS de la ROM flashée / en édition.
//
#include "ecu/OpenDamos.hpp"
#include "ecu/MapSampler.hpp"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
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
    std::string   category;            // boost, smoke, …
};

enum class ValidationStatus { Ok, Warn, Fail, NoData };

struct ValidationResult {
    QString           mapName;
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

// Charge la ROM + recipe, relocalise les maps, et évalue les règles de validation.
class TuneValidator {
public:
    void clear();
    bool loadRom(const QByteArray& rom, const QString& ecuId);

    bool isReady() const { return m_ready; }
    QString ecuId() const { return m_ecuId; }
    QString romMd5() const { return m_romMd5; }

    void setToleranceMbar(double v) { m_tolerance = v; }
    double toleranceMbar() const { return m_tolerance; }

    void setYAxisMode(YAxisMode m) { m_yMode = m; }
    YAxisMode yAxisMode() const { return m_yMode; }

    // Règles auto-générées depuis OpenDAMOS (boost + smoke par défaut).
    const std::vector<ValidationRule>& rules() const { return m_rules; }

    std::optional<ValidationResult> evaluateRule(const ValidationRule& rule,
                                                 const LivePidSnapshot& live) const;

    std::vector<ValidationResult> evaluateAll(const LivePidSnapshot& live) const;

    // Interpole une map nommée (replay / debug).
    std::optional<MapSampleResult> sampleMap(const std::string& mapName,
                                             double xPhys,
                                             double yPhys) const;

    std::optional<DamosEntry> entry(const std::string& name) const;
    std::optional<RelocResult> reloc(const std::string& name) const;

private:
    void buildRules();
    std::optional<double> measuredValue(const ValidationRule& rule,
                                        const LivePidSnapshot& live) const;
    double yPhysForRule(const ValidationRule& rule,
                        const LivePidSnapshot& live,
                        const DamosEntry& entry) const;
    ValidationStatus classify(double deltaMbar, const std::string& unit) const;

    OpenDamos m_od;
    QByteArray m_rom;
    QString   m_ecuId;
    QString   m_romMd5;
    bool      m_ready = false;
    double    m_tolerance = 50.0;   // mbar ou unité native pour DirectPid
    YAxisMode m_yMode = YAxisMode::EngineLoadPct;

    std::unordered_map<std::string, RelocResult> m_reloc;
    std::vector<ValidationRule> m_rules;
};

} // namespace ecu
