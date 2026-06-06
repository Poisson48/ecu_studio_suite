#pragma once
#include "map_entry.h"

#include <QByteArray>
#include <QColor>
#include <QString>
#include <vector>

namespace ecu { struct DamosRecipe; struct RelocResult; }

namespace ecu_studio {

// Qualité globale de relocalisation d'une recette OpenDAMOS sur une ROM donnée.
// C'est l'atout d'OpenDAMOS face à DAMOS (statique, lié à un firmware exact) :
// on mesure combien de caractéristiques ont été retrouvées par empreinte/ancre
// sur CE firmware, avec quelle confiance.
struct RelocQuality {
    enum Tier { None = 0, Poor, Partial, Good };
    int    total         = 0;   // caractéristiques de la recette
    int    relocated     = 0;   // retrouvées (hors fallback, score > 0)
    int    byFingerprint = 0;
    int    byAnchor      = 0;
    double avgScore      = 0.0; // confiance moyenne sur les relocalisées
    double fraction      = 0.0; // relocated / total
    Tier   tier          = None;
};

// Calcule la qualité de relocalisation à partir des résultats bruts d'OpenDamos.
RelocQuality computeRelocQuality(const std::vector<ecu::RelocResult>& results);

// Couleur et libellé associés à une qualité (partagés badge panel ↔ barre de statut).
QColor  relocTierColor(RelocQuality::Tier tier);
QString relocQualityText(const RelocQuality& q);   // ex. « 11/14 · empreinte · 88 % »

// Relocalise toutes les caractéristiques du recipe open_damos dans `rom` puis
// construit la liste des MapEntry correspondantes : dimensions lues à l'adresse
// résolue, conversion phys↔raw (factor/offset), unités d'axes, valeurs stock et
// libellé `matchInfo` (source + mode + confiance) renseignés.
//
// Cette fonction factorise la logique autrefois dupliquée à l'identique dans
// MapEditorPanel::refreshMaps(), ::runOpenDamos() et ::importOpenDamosRecipe().
//
// `outByFingerprint`, s'il est fourni, reçoit le nombre de maps relocalisées par
// empreinte (c.-à-d. hors AddressSource::DefaultFallback).
std::vector<MapEntry> buildRelocatedEntries(const ecu::DamosRecipe& recipe,
                                            const QByteArray& rom,
                                            int* outByFingerprint = nullptr);

} // namespace ecu_studio
