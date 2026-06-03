#include "map_view_helpers.h"

#include <QFont>
#include <QPainter>
#include <QPen>
#include <QPolygon>
#include <QRect>

#include <algorithm>

namespace ecu_studio {

QString hex32(quint32 v) {
    return QString("0x%1").arg(v, 6, 16, QChar('0')).toUpper().replace("0X", "0x");
}

QColor heatColor(double t) {
    t = std::clamp(t, 0.0, 1.0);
    double r, g, b;
    if (t < 0.25)      { double u = t / 0.25;          r = 0;     g = u;     b = 1; }
    else if (t < 0.5)  { double u = (t - 0.25) / 0.25; r = 0;     g = 1;     b = 1 - u; }
    else if (t < 0.75) { double u = (t - 0.5) / 0.25;  r = u;     g = 1;     b = 0; }
    else               { double u = (t - 0.75) / 0.25; r = 1;     g = 1 - u; b = 0; }
    // Assombrir pour le fond d'une cellule (texte clair par-dessus).
    return QColor(int(r * 150), int(g * 150), int(b * 150));
}

void HeatmapDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt,
                            const QModelIndex& idx) const {
    const QVariant bg = idx.data(Qt::BackgroundRole);
    if (bg.canConvert<QBrush>())
        p->fillRect(opt.rect, bg.value<QBrush>());
    // Cellule modifiée vs stock : liseré jaune.
    if (idx.data(Qt::UserRole).toBool()) {
        p->save();
        QPen pen(QColor("#fbbf24"));
        pen.setWidth(2);
        p->setPen(pen);
        p->drawRect(opt.rect.adjusted(1, 1, -1, -1));
        p->restore();
    }
    // Sélection : voile semi-transparent par-dessus.
    if (opt.state & QStyle::State_Selected)
        p->fillRect(opt.rect, QColor(99, 102, 241, 90));

    // Texte centré, couleur depuis ForegroundRole.
    const QVariant fg = idx.data(Qt::ForegroundRole);
    if (fg.canConvert<QBrush>())
        p->setPen(fg.value<QBrush>().color());
    else
        p->setPen(QColor(0xe5, 0xe7, 0xeb));

    const bool ghost = idx.data(Qt::UserRole + 2).toBool();
    const QVariant baseVar = idx.data(Qt::UserRole + 1);
    const QString curText = idx.data(Qt::DisplayRole).toString();

    if (!ghost || !baseVar.isValid()) {
        p->drawText(opt.rect, Qt::AlignCenter, curText);
    } else {
        // Mode fantôme : valeur courante centrée, baseline en petit sous la
        // valeur courante, et un triangle de delta (↑/↓) à droite.
        const double base = baseVar.toDouble();
        const double cur  = curText.toDouble();
        const bool changed = (cur != base);

        QRect topRect    = opt.rect.adjusted(0, 0, 0, -opt.rect.height() / 2);
        QRect bottomRect = opt.rect.adjusted(0, opt.rect.height() / 2, 0, 0);

        QFont curFont = opt.font;
        curFont.setBold(changed);
        p->save();
        p->setFont(curFont);
        p->drawText(topRect, Qt::AlignCenter, curText);
        p->restore();

        // Baseline en bas, gris translucide.
        p->save();
        QFont baseFont = opt.font;
        baseFont.setPointSizeF(std::max(7.0, baseFont.pointSizeF() - 1.5));
        baseFont.setItalic(true);
        p->setFont(baseFont);
        p->setPen(QColor(160, 174, 192, 180));
        p->drawText(bottomRect, Qt::AlignCenter,
                    QString::number(base, 'f', 0));
        p->restore();

        // Triangle delta (↑ rouge si > / ↓ vert si <) en haut-droite.
        if (changed) {
            p->save();
            const int s = std::max(4, opt.rect.height() / 6);
            QPolygon tri;
            const int x = opt.rect.right() - s - 2;
            const int y = opt.rect.top() + s + 1;
            if (cur > base) {
                tri << QPoint(x, y) << QPoint(x + 2*s, y) << QPoint(x + s, y - s);
                p->setBrush(QColor(239, 68, 68));
            } else {
                tri << QPoint(x, y - s) << QPoint(x + 2*s, y - s) << QPoint(x + s, y);
                p->setBrush(QColor(34, 197, 94));
            }
            p->setPen(Qt::NoPen);
            p->drawPolygon(tri);
            p->restore();
        }
    }
}

} // namespace ecu_studio
