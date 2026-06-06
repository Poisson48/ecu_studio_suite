#pragma once
#include <QIcon>
#include <QString>

namespace ecu_studio {

// Icônes de navigation de la sidebar, dessinées vectoriellement avec QPainter.
//
// Pourquoi pas des emojis ? Les libellés de panneaux utilisaient auparavant des
// emojis rendus comme texte (ex. 🗃 🧊 ⚙). Sur les systèmes sans police emoji
// couleur (fréquent sous Linux, et variable sous Windows), Qt affiche des carrés
// « tofu ». Dessiner les icônes en QPainter supprime toute dépendance à une
// police : rendu net et identique sur Linux et Windows, sans plugin Qt6Svg.
//
// Ids reconnus : hub, project, mpps, hex, maps, damos, 3d, automods, checksum,
// compare, versions, a2l, can, library. Pour tout autre id, renvoie une QIcon
// vide (la sidebar retombe alors sur l'ancien rendu texte).
//
// L'icône porte deux états : gris au repos, indigo lorsqu'elle est sélectionnée
// (QIcon::On), pour s'accorder au surlignage du bouton actif.
QIcon navIcon(const QString& id);

} // namespace ecu_studio
