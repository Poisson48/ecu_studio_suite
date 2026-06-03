#pragma once
#include <QString>
#include <cstdint>
#include <optional>
#include <string>

namespace ecu_studio {

// Origine d'une entrée listée dans la table des maps de MapEditorPanel.
// Lifted hors de MapEditorPanel pour être construite par des helpers réutilisables
// (cf. open_damos_relocator.h) sans dépendre du widget.
struct MapEntry {
    QString     name;
    quint32     address = 0;
    int         nx      = 0;
    int         ny      = 0;
    double      score   = 0.0;   // -1 => pas de score (map connue)
    bool        stage1  = false; // provient du catalogue Stage 1
    int         defaultPct = 0;  // pourcentage Stage 1 par défaut
    bool        openDamos  = false; // relocalisée par open_damos
    bool        fallback   = false; // adresse par défaut (pas d'empreinte)
    QString     matchInfo;          // mode de match / source (open_damos)
    // Conversion phys↔raw depuis open_damos (data.factor / data.offset)
    double      factor = 1.0;
    double      offset = 0.0;
    bool        hasConversion = false;
    // Valeurs stock issues du recipe (optionnelles)
    std::optional<int64_t> stockRaw;
    std::optional<double>  stockPhys;
    std::string            unit;        // unité physique des cellules (Nm, mg/cyc…)
    std::string            xAxisUnit;   // unité axe X (rpm, %…)
    std::string            yAxisUnit;   // unité axe Y
    std::string            description;
};

} // namespace ecu_studio
