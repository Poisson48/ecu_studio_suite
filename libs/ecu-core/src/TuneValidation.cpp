#include "ecu/TuneValidation.hpp"

#include "ecu/RomPatcher.hpp"

#include <QCryptographicHash>

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

MeasureKind defaultMeasureForCategory(const std::string& cat) {
    if (cat == "boost") return MeasureKind::MapAbsMbar;
    return MeasureKind::DirectPid;
}

std::uint8_t defaultMeasurePid(const std::string& cat) {
    if (cat == "boost") return 0x0B;
    if (cat == "smoke") return 0x24;
    return 0x0B;
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

} // namespace

void TuneValidator::clear() {
    m_ready = false;
    m_ecuId.clear();
    m_romMd5.clear();
    m_rom.clear();
    m_reloc.clear();
    m_rules.clear();
    m_od = OpenDamos{};
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

void TuneValidator::buildRules() {
    m_rules.clear();
    if (!m_od.recipe()) return;

    for (const DamosEntry& e : m_od.recipe()->characteristics) {
        if (e.type != DamosType::Map) continue;
        if (e.category != "boost" && e.category != "smoke") continue;
        if (m_reloc.find(e.name) == m_reloc.end()) continue;

        ValidationRule rule;
        rule.mapName    = e.name;
        rule.category   = e.category;
        rule.xPid       = 0x0C;
        rule.yPid       = 0x04;
        rule.yMode      = m_yMode;
        rule.measure    = defaultMeasureForCategory(e.category);
        rule.measurePid = defaultMeasurePid(e.category);
        m_rules.push_back(std::move(rule));
    }
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
    double tol = m_tolerance;
    if (unit == "λ" || unit == "lambda") tol = 0.05;
    const double a = std::abs(delta);
    if (a <= tol) return ValidationStatus::Ok;
    if (a <= tol * 2.0) return ValidationStatus::Warn;
    return ValidationStatus::Fail;
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
    out.mapName = QString::fromStdString(rule.mapName);

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
            nd.mapName = QString::fromStdString(rule.mapName);
            nd.status  = ValidationStatus::NoData;
            out.push_back(std::move(nd));
        }
    }
    return out;
}

} // namespace ecu
