#include "ecu/OpenDamosA2lExport.hpp"

#include <QMap>
#include <QSet>
#include <QStringList>

#include <cstdint>

namespace ecu {

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

QString OpenDamosA2lExport::indent(const QString& s, int n)
{
    const QString pad(n, QLatin1Char(' '));
    QStringList lines = s.split(QLatin1Char('\n'));
    for (QString& line : lines) {
        if (!line.isEmpty())
            line.prepend(pad);
    }
    return lines.join(QLatin1Char('\n'));
}

QString OpenDamosA2lExport::hexAddr(uint32_t addr)
{
    return QStringLiteral("0x") +
           QString::number(addr, 16).toUpper().rightJustified(6, QLatin1Char('0'));
}

QString OpenDamosA2lExport::quote(const QString& s)
{
    QString escaped = s;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString OpenDamosA2lExport::compuMethodId(double factor, double offset,
                                          const QString& unit)
{
    // Stable key matching the JS: `${factor}_${offset}_${unit||'NONE'}`
    // with every non-alphanumeric character replaced by '_'.
    const QString unitPart = unit.isEmpty() ? QStringLiteral("NONE") : unit;
    QString key = QString::number(factor) +
                  QLatin1Char('_') +
                  QString::number(offset) +
                  QLatin1Char('_') +
                  unitPart;

    for (QChar& ch : key) {
        if (!ch.isLetterOrNumber())
            ch = QLatin1Char('_');
    }
    return QStringLiteral("CM_") + key;
}

QString OpenDamosA2lExport::renderCompuMethod(const QString& id, double factor,
                                              double offset, const QString& unit)
{
    // ASAP2 RAT_FUNC: phys = (raw * f - c) / b
    // Choose b=1, c=-offset, f=factor so phys = raw * factor + offset.
    const double b = 1.0;
    const double c = -offset;
    const double f = factor;
    const QString unitStr = unit.isEmpty() ? QStringLiteral("-") : unit;

    const QString desc = quote(QStringLiteral("Linear conversion f=") +
                               QString::number(factor) +
                               QStringLiteral(" offset=") +
                               QString::number(offset));

    QString result;
    result += QStringLiteral("/begin COMPU_METHOD ") + id + QLatin1Char('\n');
    result += QStringLiteral("  ") + desc + QLatin1Char('\n');
    result += QStringLiteral("  RAT_FUNC\n");
    result += QStringLiteral("  \"%.3\"\n");
    result += QStringLiteral("  ") + quote(unitStr) + QLatin1Char('\n');
    result += QStringLiteral("  COEFFS 0 ") +
              QString::number(b, 'g', 15) + QLatin1Char(' ') +
              QString::number(c, 'g', 15) +
              QStringLiteral(" 0 0 ") +
              QString::number(f, 'g', 15) + QLatin1Char('\n');
    result += QStringLiteral("/end COMPU_METHOD");
    return result;
}

std::optional<QString>
OpenDamosA2lExport::renderRecordLayout(const QString& name, const QString& type)
{
    if (type == QStringLiteral("MAP")) {
        QString r;
        r += QStringLiteral("/begin RECORD_LAYOUT ") + name + QLatin1Char('\n');
        r += QStringLiteral("  NO_AXIS_PTS_X 1 UWORD\n");
        r += QStringLiteral("  NO_AXIS_PTS_Y 2 UWORD\n");
        r += QStringLiteral("  AXIS_PTS_X 3 SWORD INDEX_INCR DIRECT\n");
        r += QStringLiteral("  AXIS_PTS_Y 4 SWORD INDEX_INCR DIRECT\n");
        r += QStringLiteral("  FNC_VALUES 5 SWORD COLUMN_DIR DIRECT\n");
        r += QStringLiteral("/end RECORD_LAYOUT");
        return r;
    }
    if (type == QStringLiteral("CURVE")) {
        QString r;
        r += QStringLiteral("/begin RECORD_LAYOUT ") + name + QLatin1Char('\n');
        r += QStringLiteral("  NO_AXIS_PTS_X 1 UWORD\n");
        r += QStringLiteral("  AXIS_PTS_X 2 SWORD INDEX_INCR DIRECT\n");
        r += QStringLiteral("  FNC_VALUES 3 SWORD COLUMN_DIR DIRECT\n");
        r += QStringLiteral("/end RECORD_LAYOUT");
        return r;
    }
    if (type == QStringLiteral("VALUE")) {
        QString r;
        r += QStringLiteral("/begin RECORD_LAYOUT ") + name + QLatin1Char('\n');
        r += QStringLiteral("  FNC_VALUES 1 SWORD COLUMN_DIR DIRECT\n");
        r += QStringLiteral("/end RECORD_LAYOUT");
        return r;
    }
    return std::nullopt;
}

QString OpenDamosA2lExport::renderCharacteristic(const DamosEntry& entry,
                                                  uint32_t addr)
{
    const QString name   = QString::fromStdString(entry.name);
    const QString desc   = entry.description.empty()
                               ? name
                               : QString::fromStdString(entry.description);
    // Record layout name: entry name + "_RL" (must match what assembleA2l emits).
    const QString rl     = name + QStringLiteral("_RL");

    const double factor  = entry.data.factor;
    const double offset  = entry.data.offset;
    // Physical range over the SWORD raw domain [-32768, 32767].
    const double minPhys = -32768.0 * factor + offset;
    const double maxPhys =  32767.0 * factor + offset;

    // DamosDataInfo carries no unit string; unit is left blank, matching the
    // JS behaviour where d.unit may be undefined (treated as empty).
    const QString cmId = compuMethodId(factor, offset, QString{});

    if (entry.type == DamosType::Value) {
        QString r;
        r += QStringLiteral("/begin CHARACTERISTIC ") + name + QLatin1Char('\n');
        r += QStringLiteral("  ") + quote(desc) + QLatin1Char('\n');
        r += QStringLiteral("  VALUE\n");
        r += QStringLiteral("  ") + hexAddr(addr) + QLatin1Char('\n');
        r += QStringLiteral("  ") + rl + QLatin1Char('\n');
        r += QStringLiteral("  0\n");
        r += QStringLiteral("  ") + cmId + QLatin1Char('\n');
        r += QStringLiteral("  ") + QString::number(minPhys, 'g', 15) + QLatin1Char('\n');
        r += QStringLiteral("  ") + QString::number(maxPhys, 'g', 15) + QLatin1Char('\n');
        r += QStringLiteral("/end CHARACTERISTIC");
        return r;
    }

    // MAP or CURVE: emit one AXIS_DESCR per axis.
    QStringList axisBlocks;
    for (int i = 0; i < static_cast<int>(entry.axes.size()); ++i) {
        // DamosAxis carries only fingerprint values; the C++ model has no
        // per-axis factor/offset, so we fall back to identity (1/0) exactly
        // as the JS does with `a.factor||1` / `a.offset||0`.
        constexpr double axFactor = 1.0;
        constexpr double axOffset = 0.0;
        const double aMin  = -32768.0 * axFactor + axOffset;
        const double aMax  =  32767.0 * axFactor + axOffset;
        const QString axCm = compuMethodId(axFactor, axOffset, QString{});
        const int count    = (i == 0) ? entry.dims.nx : entry.dims.ny;

        QString axLine;
        axLine += QStringLiteral("/begin AXIS_DESCR STD_AXIS NO_INPUT_QUANTITY ");
        axLine += axCm + QLatin1Char(' ');
        axLine += QString::number(count) + QLatin1Char(' ');
        axLine += QString::number(aMin, 'g', 15) + QLatin1Char(' ');
        axLine += QString::number(aMax, 'g', 15);
        axLine += QStringLiteral(" /end AXIS_DESCR");
        axisBlocks.append(axLine);
    }

    const QString typeStr = (entry.type == DamosType::Map)
                                ? QStringLiteral("MAP")
                                : QStringLiteral("CURVE");

    QString r;
    r += QStringLiteral("/begin CHARACTERISTIC ") + name + QLatin1Char('\n');
    r += QStringLiteral("  ") + quote(desc) + QLatin1Char('\n');
    r += QStringLiteral("  ") + typeStr + QLatin1Char('\n');
    r += QStringLiteral("  ") + hexAddr(addr) + QLatin1Char('\n');
    r += QStringLiteral("  ") + rl + QLatin1Char('\n');
    r += QStringLiteral("  0\n");
    r += QStringLiteral("  ") + cmId + QLatin1Char('\n');
    r += QStringLiteral("  ") + QString::number(minPhys, 'g', 15) + QLatin1Char('\n');
    r += QStringLiteral("  ") + QString::number(maxPhys, 'g', 15) + QLatin1Char('\n');
    r += indent(axisBlocks.join(QLatin1Char('\n')), 2) + QLatin1Char('\n');
    r += QStringLiteral("/end CHARACTERISTIC");
    return r;
}

// ---------------------------------------------------------------------------
// Internal assembly helper (shared by both exportA2l() overloads).
// ---------------------------------------------------------------------------

namespace {

QString damosTypeName(DamosType t)
{
    switch (t) {
        case DamosType::Map:   return QStringLiteral("MAP");
        case DamosType::Curve: return QStringLiteral("CURVE");
        case DamosType::Value: return QStringLiteral("VALUE");
        default:               return QStringLiteral("VALUE");
    }
}

std::expected<A2lExportResult, std::string>
assembleA2l(const DamosRecipe& recipe,
            const std::vector<RelocResult>* relocResults)
{
    // Build a name->address lookup from relocation results when provided.
    QMap<QString, uint32_t> relocByName;
    if (relocResults) {
        for (const RelocResult& r : *relocResults)
            relocByName.insert(QString::fromStdString(r.name),
                               static_cast<uint32_t>(r.address));
    }

    auto resolveAddress = [&](const DamosEntry& entry) -> uint32_t {
        const QString entryName = QString::fromStdString(entry.name);
        if (relocResults) {
            const auto it = relocByName.constFind(entryName);
            if (it != relocByName.constEnd())
                return it.value();
        }
        // Baseline: parse defaultAddress (hex "0x..." or decimal string).
        bool ok = false;
        const QString da = QString::fromStdString(entry.defaultAddress);
        uint32_t resolvedAddr = 0;
        if (da.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
            resolvedAddr = static_cast<uint32_t>(da.toULongLong(&ok, 16));
        else
            resolvedAddr = static_cast<uint32_t>(da.toULongLong(&ok, 10));
        return ok ? resolvedAddr : 0u;
    };

    // -----------------------------------------------------------------------
    // Header
    // -----------------------------------------------------------------------
    const QString ecuId   = QString::fromStdString(recipe.ecuId);
    // DamosRecipe carries no standalone name/description/version fields; use
    // ecuId as the project identifier, mirroring `damos.name || 'open_damos'`.
    const QString projName = ecuId.isEmpty() ? QStringLiteral("open_damos") : ecuId;

    QStringList parts;
    parts << QStringLiteral("ASAP2_VERSION 1 60");
    parts << (QStringLiteral("/begin PROJECT OPEN_DAMOS ") +
              OpenDamosA2lExport::quote(projName));
    parts << (QStringLiteral("  /begin HEADER ") +
              OpenDamosA2lExport::quote(QString{}));
    parts << (QStringLiteral("    VERSION ") +
              OpenDamosA2lExport::quote(QStringLiteral("1.0.0")));
    parts << (QStringLiteral("    PROJECT_NO ") +
              OpenDamosA2lExport::quote(QStringLiteral("open_damos-") + ecuId));
    parts << QStringLiteral("  /end HEADER");
    parts << (QStringLiteral("  /begin MODULE ") +
              ecuId.toUpper() +
              QLatin1Char(' ') +
              OpenDamosA2lExport::quote(QStringLiteral("ECU module")));
    parts << QString{}; // blank line inside MODULE

    // -----------------------------------------------------------------------
    // RECORD_LAYOUTs — one per distinct entry, deduplicated by layout name.
    //
    // The JS source derived the type from damos.recordLayouts[name], falling
    // back to the loop variable `c` (which was out of scope — a ReferenceError
    // if the map was missing an entry).  Here we derive the type directly from
    // DamosEntry::type, which is always in scope; the bug cannot occur.
    // -----------------------------------------------------------------------
    QSet<QString> seenLayouts;
    QStringList   recordLayoutBlocks;

    for (const DamosEntry& entry : recipe.characteristics) {
        const QString rlName = QString::fromStdString(entry.name) +
                               QStringLiteral("_RL");
        if (seenLayouts.contains(rlName))
            continue;
        seenLayouts.insert(rlName);

        auto maybeBlock = OpenDamosA2lExport::renderRecordLayout(
            rlName, damosTypeName(entry.type));
        if (maybeBlock)
            recordLayoutBlocks << OpenDamosA2lExport::indent(*maybeBlock, 4);
    }

    parts << QStringLiteral("    /* ── RECORD_LAYOUTs "
                            "────────"
                            "────────"
                            "────────"
                            "─── */");
    parts << recordLayoutBlocks.join(QStringLiteral("\n\n"));

    // -----------------------------------------------------------------------
    // COMPU_METHODs — deduplicated by id.
    // -----------------------------------------------------------------------
    QMap<QString, QString> compuMethods; // ordered by id for deterministic output

    auto addCompuMethod = [&](double factor, double offset, const QString& unit) {
        const QString id = OpenDamosA2lExport::compuMethodId(factor, offset, unit);
        if (!compuMethods.contains(id))
            compuMethods.insert(id,
                OpenDamosA2lExport::renderCompuMethod(id, factor, offset, unit));
    };

    for (const DamosEntry& c : recipe.characteristics) {
        // Data (value) conversion: DamosDataInfo has no unit field; leave blank.
        addCompuMethod(c.data.factor, c.data.offset, QString{});

        // Per-axis conversion: DamosAxis carries no factor/offset in this model.
        // Use identity (1/0), matching the JS `a.factor||1` / `a.offset||0`.
        for (std::size_t i = 0; i < c.axes.size(); ++i)
            addCompuMethod(1.0, 0.0, QString{});
    }

    QStringList compuMethodBlocks;
    for (const QString& block : std::as_const(compuMethods))
        compuMethodBlocks << OpenDamosA2lExport::indent(block, 4);

    parts << QString{};
    parts << QStringLiteral("    /* ── COMPU_METHODs "
                            "────────"
                            "────────"
                            "────────"
                            "──── */");
    parts << compuMethodBlocks.join(QStringLiteral("\n\n"));

    // -----------------------------------------------------------------------
    // CHARACTERISTICs
    // -----------------------------------------------------------------------
    QStringList charBlocks;
    for (const DamosEntry& c : recipe.characteristics) {
        charBlocks << OpenDamosA2lExport::indent(
            OpenDamosA2lExport::renderCharacteristic(c, resolveAddress(c)), 4);
    }

    parts << QString{};
    parts << QStringLiteral("    /* ── CHARACTERISTICs "
                            "────────"
                            "────────"
                            "────────"
                            "─ */");
    parts << charBlocks.join(QStringLiteral("\n\n"));

    // -----------------------------------------------------------------------
    // Footer
    // -----------------------------------------------------------------------
    parts << QString{};
    parts << QStringLiteral("  /end MODULE");
    parts << QStringLiteral("/end PROJECT");

    A2lExportResult result;
    result.a2l = parts.join(QLatin1Char('\n')) + QLatin1Char('\n');
    if (relocResults)
        result.relocation = *relocResults;

    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::expected<A2lExportResult, std::string>
OpenDamosA2lExport::exportA2l(const DamosRecipe& recipe)
{
    return assembleA2l(recipe, nullptr);
}

std::expected<A2lExportResult, std::string>
OpenDamosA2lExport::exportA2l(const DamosRecipe& recipe,
                               const std::vector<RelocResult>& relocResults)
{
    return assembleA2l(recipe, &relocResults);
}

} // namespace ecu
