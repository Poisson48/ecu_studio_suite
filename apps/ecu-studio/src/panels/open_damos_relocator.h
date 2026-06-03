#pragma once
#include "map_entry.h"

#include <QByteArray>
#include <vector>

namespace ecu { struct DamosRecipe; }

namespace ecu_studio {

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
