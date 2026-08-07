#include "ecu/TuneValidation.hpp"
#include "ecu/TunePackage.hpp"

#include "ecu/RomPatcher.hpp"

#include <QCryptographicHash>

#include <algorithm>
#include <cmath>
#include <span>

namespace ecu {

namespace {

AxisScale axisScale(const DamosAxis& a) {
    return { a.factor, a.offset };
}

AxisScale dataScale(const DamosDataInfo& d) {
    return { d.factor, d.offset };
}

double axisYRangeMin(const DamosEntry& e) {
    if (e.axes.size() < 2 || e.axes[1].fingerprint.empty()) return 0.0;
    return axisToPhys(static_cast<int16_t>(e.axes[1].fingerprint.front()),
                      axisScale(e.axes[1]));
}

double axisYRangeMax(const DamosEntry& e) {
    if (e.axes.size() < 2 || e.axes[1].fingerprint.empty()) return 100.0;
    return axisToPhys(static_cast<int16_t>(e.axes[1].fingerprint.back()),
                      axisScale(e.axes[1]));
}

int hitKey(int gx, int gy) {
    return (gx << 16) ^ (gy & 0xffff);
}

} // namespace

MeasureKind TuneValidator::defaultMeasureForCategory(const std::string& cat) {
    if (cat == "boost") return MeasureKind::MapAbsMbar;
    return MeasureKind::DirectPid;
}

std::uint8_t TuneValidator::defaultMeasurePid(const std::string& cat) {
    if (cat == "boost") return 0x0B; // MAP
    if (cat == "smoke") return 0x24; // commanded AFR / equiv.
    if (cat == "air")   return 0x10; // MAF
    if (cat == "fuel")  return 0x23; // fuel rail pressure (absolu)
    if (cat == "driver") return 0x04; // charge comme proxy
    return 0x0B;
}

ValidationStatus TuneValidator::classifyDelta(double delta, double tolerance,
                                              const std::string& unit) {
    double tol = tolerance;
    if (unit == "λ" || unit == "lambda") tol = 0.05;
    else if (unit == "g/s" || unit == "g/s " || unit == "mg/stroke")
        tol = std::max(tol * 0.02, 0.5); // air/smoke : échelle différente
    else if (unit == "kPa" || unit == "bar")
        tol = std::max(tol / 10.0, 5.0);
    const double a = std::abs(delta);
    if (a <= tol) return ValidationStatus::Ok;
    if (a <= tol * 2.0) return ValidationStatus::Warn;
    return ValidationStatus::Fail;
}

void SessionRecorder::reset() {
    m_active = false;
    m_csvPath.clear();
    m_sum = SessionSummary{};
    m_hits.clear();
}

void SessionRecorder::start(const QString& ecuId, const QString& romMd5) {
    reset();
    m_active = true;
    m_sum.ecuId = ecuId;
    m_sum.romMd5 = romMd5;
    m_sum.started = QDateTime::currentDateTime();
}

void SessionRecorder::ingest(const std::vector<ValidationResult>& results) {
    if (!m_active) return;
    ++m_sum.ticks;

    const ValidationResult* primary = nullptr;
    for (const auto& r : results) {
        switch (r.status) {
            case ValidationStatus::Ok:     ++m_sum.ok; break;
            case ValidationStatus::Warn:   ++m_sum.warn; break;
            case ValidationStatus::Fail:   ++m_sum.fail; break;
            case ValidationStatus::NoData: ++m_sum.noData; break;
        }
        if (r.status == ValidationStatus::NoData) continue;
        const double ad = std::abs(r.delta);
        if (ad > m_sum.peakAbsDelta) {
            m_sum.peakAbsDelta = ad;
            m_sum.peakMap = r.mapName;
        }
        if (!primary) primary = &r;
        if (r.category == QStringLiteral("boost")
            || r.mapName.contains(QStringLiteral("boost"), Qt::CaseInsensitive)
            || r.mapName.contains(QStringLiteral("pAirBas"), Qt::CaseInsensitive))
            primary = &r;
    }

    if (primary && primary->status != ValidationStatus::NoData) {
        const int k = hitKey(primary->ix0, primary->iy0);
        auto& cell = m_hits[k];
        cell.gx = primary->ix0;
        cell.gy = primary->iy0;
        ++cell.count;
        cell.sumAbsDelta += std::abs(primary->delta);
    }
}

SessionSummary SessionRecorder::finish() {
    m_sum.ended = QDateTime::currentDateTime();
    m_sum.csvPath = m_csvPath;
    m_sum.hotspots.clear();
    m_sum.hotspots.reserve(m_hits.size());
    for (auto& [_, cell] : m_hits)
        m_sum.hotspots.push_back(cell);
    std::sort(m_sum.hotspots.begin(), m_sum.hotspots.end(),
              [](const SessionHitCell& a, const SessionHitCell& b) {
                  return a.meanAbsDelta() > b.meanAbsDelta();
              });
    if (m_sum.hotspots.size() > 12)
        m_sum.hotspots.resize(12);
    m_active = false;
    return m_sum;
}

void StatusHysteresis::reset() {
    m_failStreak = 0;
    m_okStreak = 0;
    m_shown = ValidationStatus::NoData;
}

ValidationStatus StatusHysteresis::update(ValidationStatus raw) {
    if (raw == ValidationStatus::NoData)
        return m_shown;

    if (raw == ValidationStatus::Fail) {
        ++m_failStreak;
        m_okStreak = 0;
        if (m_failStreak >= m_failNeed)
            m_shown = ValidationStatus::Fail;
        else if (m_shown == ValidationStatus::NoData)
            m_shown = ValidationStatus::Warn;
        return m_shown;
    }

    if (raw == ValidationStatus::Warn) {
        m_okStreak = 0;
        if (m_shown != ValidationStatus::Fail)
            m_shown = ValidationStatus::Warn;
        return m_shown;
    }

    // Ok
    m_failStreak = 0;
    ++m_okStreak;
    if (m_shown == ValidationStatus::Fail || m_shown == ValidationStatus::Warn) {
        if (m_okStreak >= m_okNeed)
            m_shown = ValidationStatus::Ok;
    } else {
        m_shown = ValidationStatus::Ok;
    }
    return m_shown;
}

void TuneValidator::clear() {
    m_ready = false;
    m_ecuId.clear();
    m_romMd5.clear();
    m_rom.clear();
    m_reloc.clear();
    m_rules.clear();
    m_od = OpenDamos{};
}

bool TuneValidator::categoryEnabled(const std::string& cat) const {
    return std::find(m_categories.begin(), m_categories.end(), cat) != m_categories.end();
}

void TuneValidator::setEnabledCategories(std::vector<std::string> cats) {
    m_categories = std::move(cats);
    if (m_od.recipe()) buildRules();
    m_ready = !m_rules.empty();
}

void TuneValidator::setRuleEnabled(const std::string& mapName, bool on) {
    for (auto& r : m_rules)
        if (r.mapName == mapName) r.enabled = on;
}

void TuneValidator::setRules(std::vector<ValidationRule> rules) {
    m_rules = std::move(rules);
    for (auto& r : m_rules)
        r.yMode = m_yMode;
    m_ready = !m_rules.empty();
}

bool TuneValidator::loadRom(const QByteArray& rom, const QString& ecuId) {
    clear();
    if (rom.isEmpty() || ecuId.isEmpty()) return false;

    auto recipe = OpenDamos::loadRecipe(ecuId);
    if (!recipe) return false;

    m_od.setRecipe(std::move(*recipe));
    m_ecuId  = ecuId;
    m_rom    = rom;
    m_romMd5 = QString::fromLatin1(
        QCryptographicHash::hash(rom, QCryptographicHash::Md5).toHex());

    for (auto& r : m_od.relocate(rom))
        m_reloc.emplace(r.name, std::move(r));

    buildRules();
    m_ready = !m_rules.empty();
    return m_ready;
}

bool TuneValidator::loadRomWithRecipe(const QByteArray& rom, const QString& ecuId,
                                      const QByteArray& recipeJsonUtf8) {
    clear();
    if (rom.isEmpty() || ecuId.isEmpty() || recipeJsonUtf8.isEmpty()) return false;

    auto recipe = OpenDamos::parseRecipe(recipeJsonUtf8.toStdString());
    if (!recipe) return false;

    m_od.setRecipe(std::move(*recipe));
    m_ecuId  = ecuId;
    m_rom    = rom;
    m_romMd5 = QString::fromLatin1(
        QCryptographicHash::hash(rom, QCryptographicHash::Md5).toHex());

    for (auto& r : m_od.relocate(rom))
        m_reloc.emplace(r.name, std::move(r));

    buildRules();
    m_ready = !m_rules.empty();
    return m_ready;
}

bool TuneValidator::loadTunePackage(const TunePackage& pkg) {
    return loadRomWithRecipe(pkg.rom, pkg.manifest.ecuId, pkg.recipeJson);
}

void TuneValidator::buildRules() {
    m_rules.clear();
    if (!m_od.recipe()) return;

    for (const DamosEntry& e : m_od.recipe()->characteristics) {
        if (e.type != DamosType::Map) continue;
        if (!categoryEnabled(e.category)) continue;
        if (m_reloc.find(e.name) == m_reloc.end()) continue;

        ValidationRule rule;
        rule.mapName    = e.name;
        rule.category   = e.category;
        rule.xPid       = 0x0C;
        rule.yPid       = 0x04;
        rule.yMode      = m_yMode;
        rule.measure    = defaultMeasureForCategory(e.category);
        rule.measurePid = defaultMeasurePid(e.category);
        rule.enabled    = true;
        m_rules.push_back(std::move(rule));
    }
}

std::vector<std::uint8_t> TuneValidator::requiredPids() const {
    std::vector<std::uint8_t> out;
    auto add = [&](std::uint8_t p) {
        if (std::find(out.begin(), out.end(), p) == out.end())
            out.push_back(p);
    };
    add(0x0C); // RPM toujours
    add(0x04); // charge
    add(0x0D); // vitesse (contexte)
    for (const auto& r : m_rules) {
        if (!r.enabled) continue;
        add(r.xPid);
        add(r.yPid);
        if (r.measure == MeasureKind::MapAbsMbar) {
            add(0x0B);
            add(0x33); // baro
        } else {
            add(r.measurePid);
        }
    }
    return out;
}

std::optional<DamosEntry> TuneValidator::entry(const std::string& name) const {
    if (!m_od.recipe()) return std::nullopt;
    for (const DamosEntry& e : m_od.recipe()->characteristics)
        if (e.name == name) return e;
    return std::nullopt;
}

std::optional<RelocResult> TuneValidator::reloc(const std::string& name) const {
    const auto it = m_reloc.find(name);
    if (it == m_reloc.end()) return std::nullopt;
    return it->second;
}

double TuneValidator::yPhysForRule(const ValidationRule& rule,
                                   const LivePidSnapshot& live,
                                   const DamosEntry& e) const {
    const auto yIt = live.find(rule.yPid);
    if (yIt == live.end()) return 0.0;

    if (rule.yMode == YAxisMode::EngineLoadPct && e.axes.size() >= 2) {
        const double yMin = axisYRangeMin(e);
        const double yMax = axisYRangeMax(e);
        const double load = std::clamp(yIt->second, 0.0, 100.0);
        if (yMax <= yMin) return load;
        return yMin + (load / 100.0) * (yMax - yMin);
    }

    return yIt->second;
}

std::optional<double> TuneValidator::measuredValue(const ValidationRule& rule,
                                                   const LivePidSnapshot& live) const {
    switch (rule.measure) {
        case MeasureKind::MapAbsMbar: {
            const auto mapIt = live.find(0x0B);
            if (mapIt == live.end()) return std::nullopt;
            const auto baroIt = live.find(0x33);
            const double baro = baroIt != live.end() ? baroIt->second : 101.3;
            return (mapIt->second + baro) * 10.0;
        }
        case MeasureKind::DirectPid: {
            const auto it = live.find(rule.measurePid);
            if (it == live.end()) return std::nullopt;
            return it->second;
        }
    }
    return std::nullopt;
}

ValidationStatus TuneValidator::classify(double delta, const std::string& unit) const {
    return classifyDelta(delta, m_tolerance, unit);
}

std::optional<MapSampleResult> TuneValidator::sampleMap(const std::string& mapName,
                                                        double xPhys,
                                                        double yPhys) const {
    const auto ent = entry(mapName);
    const auto rel = reloc(mapName);
    if (!ent || !rel || ent->axes.size() < 2 || m_rom.isEmpty()) return std::nullopt;

    const auto md = readMapData(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(m_rom.constData()),
        static_cast<std::size_t>(m_rom.size())), rel->address);
    if (!md) return std::nullopt;

    return sampleMapBilinear(*md,
                             axisScale(ent->axes[0]),
                             axisScale(ent->axes[1]),
                             dataScale(ent->data),
                             xPhys, yPhys);
}

std::optional<ValidationResult> TuneValidator::evaluateRule(const ValidationRule& rule,
                                                            const LivePidSnapshot& live) const {
    ValidationResult out;
    out.mapName  = QString::fromStdString(rule.mapName);
    out.category = QString::fromStdString(rule.category);

    if (!rule.enabled) {
        out.status = ValidationStatus::NoData;
        return out;
    }

    const auto ent = entry(rule.mapName);
    const auto rel = reloc(rule.mapName);
    if (!ent || !rel || m_rom.isEmpty()) return std::nullopt;

    const auto xIt = live.find(rule.xPid);
    if (xIt == live.end()) return std::nullopt;

    const auto meas = measuredValue(rule, live);
    if (!meas) return std::nullopt;

    const double yPhys = yPhysForRule(rule, live, *ent);
    const auto sampled = sampleMap(rule.mapName, xIt->second, yPhys);
    if (!sampled) return std::nullopt;

    out.measured     = *meas;
    out.expected     = sampled->value;
    out.delta        = out.measured - out.expected;
    out.unit         = QString::fromStdString(ent->data.unit);
    out.xPhys        = xIt->second;
    out.yPhys        = yPhys;
    out.ix0          = sampled->ix0;
    out.iy0          = sampled->iy0;
    out.mapAddress   = rel->address;
    out.status       = classify(out.delta, ent->data.unit);
    if (!ent->axes.empty())    out.xUnit = QString::fromStdString(ent->axes[0].unit);
    if (ent->axes.size() >= 2) out.yUnit = QString::fromStdString(ent->axes[1].unit);
    return out;
}

std::vector<ValidationResult> TuneValidator::evaluateAll(const LivePidSnapshot& live) const {
    std::vector<ValidationResult> out;
    out.reserve(m_rules.size());
    for (const ValidationRule& rule : m_rules) {
        if (auto r = evaluateRule(rule, live))
            out.push_back(std::move(*r));
        else {
            ValidationResult nd;
            nd.mapName  = QString::fromStdString(rule.mapName);
            nd.category = QString::fromStdString(rule.category);
            nd.status   = ValidationStatus::NoData;
            out.push_back(std::move(nd));
        }
    }
    return out;
}

} // namespace ecu
