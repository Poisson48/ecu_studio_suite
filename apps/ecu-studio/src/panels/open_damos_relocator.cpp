#include "open_damos_relocator.h"
#include "../byte_span.h"

#include <QByteArrayView>
#include <QCoreApplication>
#include <QString>

#include <algorithm>

#include "ecu/OpenDamos.hpp"
#include "ecu/RomPatcher.hpp"

namespace ecu_studio {

namespace {

// Conserve le contexte de traduction historique des chaînes (anciennement
// produites par MapEditorPanel::tr) pour que les .ts existants restent valides.
QString trCtx(const char* s) {
    return QCoreApplication::translate("ecu_studio::MapEditorPanel", s);
}

// Renseigne conversion phys↔raw, unités, stock et description d'une entrée à
// partir de la caractéristique homonyme du recipe (si trouvée).
void fillFromRecipe(MapEntry& e, const ecu::DamosRecipe& recipe,
                    const std::string& name) {
    auto it = std::find_if(recipe.characteristics.begin(),
                           recipe.characteristics.end(),
                           [&](const ecu::DamosEntry& de) { return de.name == name; });
    if (it == recipe.characteristics.end())
        return;
    e.factor        = it->data.factor;
    e.offset        = it->data.offset;
    e.hasConversion = (e.factor != 1.0 || e.offset != 0.0);
    e.stockRaw      = it->stockRawValue;
    e.stockPhys     = it->stockPhysValue;
    e.description   = it->description;
    e.unit          = it->data.unit;
    if (!it->axes.empty()) e.xAxisUnit = it->axes[0].unit;
    if (it->axes.size() > 1) e.yAxisUnit = it->axes[1].unit;
}

// Libellé de la colonne « Score » : source + mode + confiance.
QString matchInfoFor(const ecu::RelocResult& r) {
    QString src;
    switch (r.addressSource) {
        case ecu::AddressSource::Fingerprint:     src = trCtx("empreinte"); break;
        case ecu::AddressSource::Anchor:          src = trCtx("ancre");     break;
        case ecu::AddressSource::DefaultFallback: src = trCtx("défaut");    break;
    }
    const QString mode = QString::fromStdString(r.matchMode);
    return mode.isEmpty()
        ? QString("%1 (%2)").arg(src).arg(r.score, 0, 'f', 2)
        : QString("%1 %2 (%3)").arg(src, mode).arg(r.score, 0, 'f', 2);
}

} // namespace

std::vector<MapEntry> buildRelocatedEntries(const ecu::DamosRecipe& recipe,
                                            const QByteArray& rom,
                                            int* outByFingerprint) {
    QByteArrayView view(rom.constData(), rom.size());
    auto results = ecu::OpenDamos{}.relocate(recipe, view);

    auto span = constByteSpan(rom);
    std::vector<MapEntry> entries;
    entries.reserve(results.size());
    int byFingerprint = 0;

    for (const auto& r : results) {
        const bool fallback = (r.addressSource == ecu::AddressSource::DefaultFallback);
        if (!fallback) ++byFingerprint;

        MapEntry e;
        e.name      = QString::fromStdString(r.name);
        e.address   = static_cast<quint32>(r.address);
        e.score     = r.score;
        e.openDamos = true;
        e.fallback  = fallback;

        if (auto md = ecu::readMapData(span, e.address)) {
            e.nx = md->nx;
            e.ny = md->ny;
        }

        fillFromRecipe(e, recipe, r.name);
        e.matchInfo = matchInfoFor(r);

        entries.push_back(std::move(e));
    }

    if (outByFingerprint) *outByFingerprint = byFingerprint;
    return entries;
}

RelocQuality computeRelocQuality(const std::vector<ecu::RelocResult>& results) {
    RelocQuality q;
    q.total = static_cast<int>(results.size());
    double scoreSum = 0.0;
    for (const auto& r : results) {
        const bool relocated =
            r.addressSource != ecu::AddressSource::DefaultFallback && r.score != 0.0;
        if (!relocated) continue;
        ++q.relocated;
        scoreSum += r.score;
        if (r.addressSource == ecu::AddressSource::Fingerprint) ++q.byFingerprint;
        else if (r.addressSource == ecu::AddressSource::Anchor)  ++q.byAnchor;
    }
    if (q.relocated > 0) q.avgScore = scoreSum / q.relocated;
    if (q.total > 0)     q.fraction = static_cast<double>(q.relocated) / q.total;

    if (q.total == 0)
        q.tier = RelocQuality::None;
    else if (q.fraction >= 0.85 && q.avgScore >= 0.70)
        q.tier = RelocQuality::Good;
    else if (q.fraction >= 0.50)
        q.tier = RelocQuality::Partial;
    else
        q.tier = RelocQuality::Poor;
    return q;
}

QColor relocTierColor(RelocQuality::Tier tier) {
    switch (tier) {
        case RelocQuality::Good:    return QColor(0x22, 0xc5, 0x5e);  // vert
        case RelocQuality::Partial: return QColor(0xf5, 0x9e, 0x0b);  // orange
        case RelocQuality::Poor:    return QColor(0xef, 0x44, 0x44);  // rouge
        case RelocQuality::None:    break;
    }
    return QColor(0x6b, 0x72, 0x80);                                  // gris
}

QString relocQualityText(const RelocQuality& q) {
    if (q.total == 0)
        return QCoreApplication::translate("ecu_studio", "non relocalisé");
    const int pct = static_cast<int>(q.avgScore * 100.0 + 0.5);
    return QCoreApplication::translate("ecu_studio", "%1/%2 maps · %3 %")
        .arg(q.relocated).arg(q.total).arg(pct);
}

} // namespace ecu_studio
