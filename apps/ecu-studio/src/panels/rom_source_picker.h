#pragma once
#include <QByteArray>
#include <QString>

class QWidget;

namespace ecu_studio {

// Résultat d'un choix de source de ROM (référence du mode fantôme ou opérande de
// comparaison). `ok` est false si l'utilisateur a annulé.
struct PickedRom {
    bool       ok          = false;
    bool       firstOption = false;  // true = la 1re option a été choisie (sens
                                     // selon l'appelant : ROM courante / origine)
    QByteArray bytes;                // octets choisis (commit/fichier) sinon vide
    QString    label;                // libellé : nom de fichier ou « commit xxxx »
};

// Ouvre un dialogue modal proposant jusqu'à trois sources de ROM :
//   1. une première option spécifique à l'appelant (bouton affiché seulement si
//      `firstOptionLabel` est non vide) — ex. « ROM courante du document » pour
//      la comparaison, ou « Snapshot d'origine » pour la baseline ;
//   2. un commit git du dépôt contenant `romPath` (combo des derniers commits) ;
//   3. un fichier .bin sur disque.
//
// `romPath` localise le dépôt git et le chemin relatif de la ROM ; passe une
// chaîne vide pour désactiver l'option commit (seul le fichier reste alors
// disponible). Renvoie le choix (ok=false si annulé).
PickedRom pickRomSource(QWidget* parent, const QString& title,
                        const QString& romPath, const QString& firstOptionLabel);

} // namespace ecu_studio
