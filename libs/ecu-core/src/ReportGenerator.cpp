#include "ecu/ReportGenerator.hpp"

#include <QDateTime>
#include <QLocale>
#include <cmath>

namespace ecu {

// ── Static helpers ────────────────────────────────────────────────────────────

QString ReportGenerator::escapeHtml(const QString& s)
{
    // Five characters that must be entity-encoded inside HTML text/attributes.
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        switch (c.unicode()) {
            case '&':  out += u"&amp;"_qs;  break;
            case '<':  out += u"&lt;"_qs;   break;
            case '>':  out += u"&gt;"_qs;   break;
            case '"':  out += u"&quot;"_qs; break;
            case '\'': out += u"&#39;"_qs;  break;
            default:   out += c;            break;
        }
    }
    return out;
}

QString ReportGenerator::formatPct(const std::optional<AvgChange>& avg)
{
    if (!avg || !std::isfinite(avg->avgRatio))
        return {};

    const int pct = static_cast<int>(std::round(avg->avgRatio * 100.0));
    return (pct >= 0 ? u"+"_qs : QString{}) + QString::number(pct) + u" %"_qs;
}

// Inline CSS kept verbatim from the JS source (converted to a C++ raw literal).
// Identical selector names, values and @page / @media rules are preserved so
// that the PDF output (Ctrl-P in the browser) matches the original spec.
QString ReportGenerator::buildCss()
{
    return uR"css(
  @page { size: A4; margin: 1.5cm 1.2cm; }
  body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif; color: #222; font-size: 12px; line-height: 1.45; margin: 0; padding: 20px; }
  h1 { font-size: 22px; margin: 0 0 4px 0; border-bottom: 2px solid #335cff; padding-bottom: 6px; }
  h1 small { font-weight: normal; color: #777; font-size: 13px; }
  h2 { font-size: 15px; margin: 20px 0 8px 0; color: #335cff; }
  table { border-collapse: collapse; width: 100%; font-size: 11px; }
  table.meta td { padding: 3px 8px; vertical-align: top; border: 0; }
  table.meta td:first-child { font-weight: 600; color: #555; width: 120px; }
  table.maps { margin-top: 8px; }
  table.maps th { background: #f3f4f7; text-align: left; padding: 6px 8px; border-bottom: 1px solid #ccc; }
  table.maps td { padding: 5px 8px; border-bottom: 1px solid #eee; vertical-align: top; }
  .mono { font-family: 'SFMono-Regular', 'Menlo', monospace; font-size: 10px; }
  .num { text-align: right; font-variant-numeric: tabular-nums; }
  .up { color: #0a7a0a; font-weight: 600; }
  .down { color: #a02020; font-weight: 600; }
  .map-name { font-family: 'SFMono-Regular', 'Menlo', monospace; font-size: 10px; font-weight: 600; }
  .desc { color: #555; font-style: italic; max-width: 260px; }
  .empty { padding: 24px; text-align: center; color: #888; font-style: italic; border: 1px dashed #ccc; }
  .footer { margin-top: 32px; padding-top: 10px; border-top: 1px solid #ccc; font-size: 10px; color: #888; }
  .print-btn { position: fixed; top: 16px; right: 16px; padding: 8px 14px; background: #335cff; color: #fff; border: 0; border-radius: 4px; cursor: pointer; font-size: 13px; }
  @media print { .print-btn { display: none; } body { padding: 0; } }
)css"_qs;
}

QString ReportGenerator::buildMetaRows(const ReportProject& p,
                                       const QString& generatedAt)
{
    // Inline table: label + value pairs. Empty/missing values fall back to "—"
    // exactly as the JS does with `|| '—'`.
    auto fallback = [](const QString& v) -> QString {
        return v.isEmpty() ? u"—"_qs : v;
    };

    const QString headShort = p.headHash.isEmpty()
                                  ? u"—"_qs
                                  : p.headHash.left(12);

    // Row definitions match JS metaRows array order.
    const QList<std::pair<QString, QString>> rows = {
        { u"Projet"_qs,    p.name                                   },
        { u"Véhicule"_qs,  fallback(p.vehicle)                      },
        { u"Immat"_qs,     fallback(p.immat)                        },
        { u"Année"_qs,     fallback(p.year)                         },
        { u"ECU"_qs,       p.ecu.isEmpty() ? u"—"_qs : p.ecu.toUpper() },
        { u"ROM"_qs,       fallback(p.romName)                      },
        { u"Branche"_qs,   fallback(p.branchName)                   },
        { u"HEAD"_qs,      headShort                                 },
        { u"Généré le"_qs, generatedAt                               },
    };

    QString out;
    for (const auto& [k, v] : rows) {
        out += u"  <tr><td>"_qs + escapeHtml(k)
             + u"</td><td>"_qs + escapeHtml(v)
             + u"</td></tr>\n"_qs;
    }
    return out;
}

QString ReportGenerator::buildMapRows(const std::vector<MapDiffResult>& maps)
{
    QString rows;
    rows.reserve(static_cast<qsizetype>(maps.size()) * 512);

    for (const MapDiffResult& m : maps) {
        // Address formatted as 0xXXXXXX (6 hex digits, upper-case, zero-padded).
        const QString addr =
            u"0x"_qs + QString::number(m.address, 16).toUpper().rightJustified(6, u'0');

        const QString delta = formatPct(m.avg);

        // delta CSS class: "up" for positive, "down" for negative, blank otherwise.
        QString deltaClass;
        if (delta.startsWith(u'+'))
            deltaClass = u"up"_qs;
        else if (delta.startsWith(u'-'))
            deltaClass = u"down"_qs;

        // sample: "before → after" or "—" when absent.
        QString sample;
        if (m.sample)
            sample = QString::number(m.sample->before)
                   + u" → "_qs   // → (U+2192 RIGHTWARDS ARROW)
                   + QString::number(m.sample->after);
        else
            sample = u"—"_qs;

        // Description truncated to 120 chars (already done by mapsChanged, but
        // guard here for direct callers that bypass mapsChanged).
        const QString desc = escapeHtml(
            QString::fromStdString(m.description).left(120));

        rows += u"      <tr>\n"_qs
              + u"        <td class=\"map-name\">"_qs + escapeHtml(QString::fromStdString(m.name)) + u"</td>\n"_qs;

        // CharType → display string (mirrors JS m.type which is a plain string
        // in the JS, but here it's the enum value from MapDiffer.hpp).
        QString typeStr;
        switch (m.type) {
            case CharType::VALUE:   typeStr = u"VALUE"_qs;   break;
            case CharType::VAL_BLK: typeStr = u"VAL_BLK"_qs; break;
            case CharType::CURVE:   typeStr = u"CURVE"_qs;   break;
            case CharType::MAP:     typeStr = u"MAP"_qs;     break;
            default:                typeStr = u"OTHER"_qs;   break;
        }

        rows += u"        <td>"_qs + escapeHtml(typeStr) + u"</td>\n"_qs
              + u"        <td class=\"mono\">"_qs + addr + u"</td>\n"_qs
              + u"        <td class=\"num\">"_qs
                    + QString::number(m.cellsChanged) + u"/"_qs
                    + QString::number(m.totalCells) + u"</td>\n"_qs
              + u"        <td class=\"num "_qs + deltaClass + u"\">"_qs
                    + escapeHtml(delta) + u"</td>\n"_qs
              + u"        <td class=\"mono\">"_qs + escapeHtml(sample) + u"</td>\n"_qs
              + u"        <td>"_qs + escapeHtml(QString::fromStdString(m.unit)) + u"</td>\n"_qs
              + u"        <td class=\"desc\">"_qs + desc + u"</td>\n"_qs
              + u"      </tr>\n"_qs;
    }
    return rows;
}

// ── Public API ────────────────────────────────────────────────────────────────

std::optional<QString> ReportGenerator::generate(const ReportInput& input) const
{
    const MapsChangedResult diffResult =
        mapsChanged(input.originalBuf, input.currentBuf, input.characteristics);
    const std::vector<MapDiffResult>& maps = diffResult.maps;

    // Locale-aware date/time string: "long" date style + "short" time style,
    // French locale — mirrors JS toLocaleString('fr-FR', …).
    const QLocale frLocale(QLocale::French, QLocale::France);
    const QDateTime now = QDateTime::currentDateTime();
    const QString generatedAt =
        frLocale.toString(now.date(), QLocale::LongFormat)
        + u" "_qs
        + frLocale.toString(now.time(), QLocale::ShortFormat);

    const QString escapedProjectName = escapeHtml(input.project.name);
    const QString metaRows           = buildMetaRows(input.project, generatedAt);
    const QString mapRows            = buildMapRows(maps);
    const QString css                = buildCss();

    // Maps section: either the table or the "no changes" placeholder.
    QString mapsSection;
    if (maps.empty()) {
        mapsSection =
            u"<div class=\"empty\">Aucune modification détectée entre la ROM actuelle et la ROM originale.</div>\n"_qs;
    } else {
        mapsSection =
            u"<table class=\"maps\">\n"_qs
            u"  <thead><tr>\n"_qs
            u"    <th>Nom</th><th>Type</th><th>Adresse</th><th>Cellules</th>"
            u"<th>Δ moyen</th><th>Échantillon raw</th><th>Unité</th><th>Description</th>\n"_qs
            u"  </tr></thead>\n"_qs
            u"  <tbody>"_qs + mapRows + u"</tbody>\n"_qs
            u"</table>\n"_qs;
    }

    // Maps count header label (JS: `Cartes modifiées (${maps.length})`).
    const QString mapsCountLabel =
        u"Cartes modifiées ("_qs
        + QString::number(static_cast<qsizetype>(maps.size()))
        + u")"_qs;

    QString html;
    html.reserve(8192);
    html =
        u"<!doctype html>\n"_qs
        u"<html lang=\"fr\">\n"_qs
        u"<head>\n"_qs
        u"<meta charset=\"utf-8\">\n"_qs
        u"<title>Rapport tune — "_qs + escapedProjectName + u"</title>\n"_qs
        u"<style>"_qs + css + u"</style>\n"_qs
        u"</head>\n"_qs
        u"<body>\n"_qs
        u"<button class=\"print-btn\" onclick=\"window.print()\">\xf0\x9f\x96\xa8 Imprimer / PDF</button>\n\n"_qs
        u"<h1>Rapport tune <small>"_qs + escapedProjectName + u"</small></h1>\n\n"_qs
        u"<h2>Informations projet</h2>\n"_qs
        u"<table class=\"meta\">\n"_qs
        + metaRows +
        u"</table>\n\n"_qs
        u"<h2>"_qs + escapeHtml(mapsCountLabel)
        + u" <small style=\"font-weight:normal;color:#777\">— par rapport à la ROM originale</small></h2>\n\n"_qs
        + mapsSection +
        u"\n<div class=\"footer\">\n"_qs
        u"  Généré par open-car-reprog · "_qs
        + escapeHtml(generatedAt)
        + u" · Δ moyen = variation relative moyenne sur toutes les cellules modifiées.\n"_qs
        u"</div>\n"_qs
        u"</body>\n"_qs
        u"</html>"_qs;

    return html;
}

} // namespace ecu
