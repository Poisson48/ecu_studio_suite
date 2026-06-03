#pragma once
#include <QColor>
#include <QString>
#include <QStyledItemDelegate>

namespace ecu_studio {

// Format hexadécimal compact d'une adresse ROM (« 0x01ABCD »).
QString hex32(quint32 v);

// Couleur froid→chaud (bleu → cyan → vert → jaune → rouge) pour t∈[0,1].
// Teintes assombries pour rester lisibles sur le thème sombre.
QColor heatColor(double t);

// Delegate qui peint manuellement le fond depuis Qt::BackgroundRole. Contourne
// le piège Qt classique : dès qu'une stylesheet globale stylise QTableWidget::item,
// les setBackground() programmatiques sont ignorés. On peint nous-mêmes.
//
// Rôles supplémentaires :
//   Qt::UserRole       -> bool, cellule modifiée vs stock (liseré jaune)
//   Qt::UserRole + 1   -> double, valeur baseline si mode fantôme actif (NaN sinon)
//   Qt::UserRole + 2   -> bool, mode fantôme activé pour cette cellule
class HeatmapDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override;
};

} // namespace ecu_studio
