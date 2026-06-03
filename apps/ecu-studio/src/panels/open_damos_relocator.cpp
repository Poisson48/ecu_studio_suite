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

} // namespace ecu_studio
