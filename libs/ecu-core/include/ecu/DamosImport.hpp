#pragma once
//
// DamosImport — convertit un DAMOS Bosch (fichier ASAP2 .a2l) + sa ROM en une
// recette OpenDAMOS (le format propre de l'app).
//
// Un « DAMOS » est, en pratique, un .a2l ASAP2 (généré par DAMOS++, ASAP2
// Updater, …) décrivant les caractéristiques (MAP/CURVE/VALUE), leurs adresses,
// leurs axes et leurs conversions. Le A2lParser résout déjà toute cette
// structure ; il reste à LIRE les valeurs d'axes dans la ROM pour en faire des
// empreintes (fingerprints) — c'est ce qui rend la recette indépendante du
// firmware (l'atout d'OpenDAMOS face à DAMOS, lié à une adresse figée).
//
// Port C++ fidèle de scripts/damos_a2l_to_opendamos.py (implémentation de
// référence de docs/opendamos/CONVENTION_DAMOS_VERS_OPENDAMOS.md).
//
#include "ecu/A2lParser.hpp"
#include "ecu/OpenDamos.hpp"

#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QStringList>

namespace ecu {

struct DamosImportStats {
    int         converted = 0;   // caractéristiques converties avec empreinte
    int         skipped   = 0;   // ignorées (hors ROM, type non géré, …)
    QStringList warnings;        // une ligne par caractéristique ignorée
};

// Construit une recette OpenDAMOS depuis les caractéristiques d'un DAMOS (A2L
// déjà parsé) et sa ROM. Pour chaque MAP/CURVE on lit les axes dans la ROM
// (COM_AXIS via l'adresse du bloc d'axe, sinon en-tête inline) → fingerprint.
//
// `rom` doit être une image flash basée en physique : les adresses A2L sont
// normalisées sur les 29 bits de poids faible (miroirs TriCore/PowerPC) pour
// indexer dedans. Ne lève jamais d'exception ; toute lecture hors-bornes est
// ignorée proprement (caractéristique comptée dans `skipped`).
DamosRecipe damosToOpenDamos(const QList<Characteristic>& chars,
                             QByteArrayView                rom,
                             const QString&                ecuId,
                             DamosImportStats*             stats = nullptr);

} // namespace ecu
