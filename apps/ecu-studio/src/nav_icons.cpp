#include "nav_icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QStringList>
#include <QtMath>

namespace ecu_studio {
namespace {

// Toutes les icônes sont dessinées dans un repère logique 24×24, puis mises à
// l'échelle vers la taille de rendu réelle. Le trait est arrondi pour un aspect
// net et homogène.

void drawDot(QPainter& p, qreal x, qreal y, qreal r) {
    p.save();
    p.setBrush(p.pen().color());
    p.drawEllipse(QPointF(x, y), r, r);
    p.restore();
}

void paintIcon(QPainter& p, const QString& id) {
    if (id == "hub") {
        // Tableau de bord : 2×2 tuiles arrondies.
        for (qreal ry : {4.0, 13.0})
            for (qreal rx : {4.0, 13.0})
                p.drawRoundedRect(QRectF(rx, ry, 7, 7), 1.6, 1.6);

    } else if (id == "project") {
        // Dossier.
        p.drawRoundedRect(QRectF(3, 8, 18, 11.5), 1.6, 1.6);
        QPainterPath tab;
        tab.moveTo(3.6, 8);
        tab.lineTo(4.6, 5.4);
        tab.lineTo(9.6, 5.4);
        tab.lineTo(10.8, 8);
        p.drawPath(tab);

    } else if (id == "mpps") {
        // Connecteur / fiche (programmation matérielle).
        p.drawLine(QPointF(9, 3), QPointF(9, 7));
        p.drawLine(QPointF(15, 3), QPointF(15, 7));
        p.drawRoundedRect(QRectF(7, 7, 10, 7), 2, 2);
        p.drawLine(QPointF(12, 14), QPointF(12, 21));

    } else if (id == "hex") {
        // Éditeur hexadécimal : fenêtre + lignes d'octets.
        p.drawRoundedRect(QRectF(3.5, 4.5, 17, 15), 2, 2);
        for (qreal y : {9.0, 12.5, 16.0}) {
            p.drawLine(QPointF(6, y), QPointF(10, y));
            p.drawLine(QPointF(12, y), QPointF(15, y));
            p.drawLine(QPointF(16.8, y), QPointF(18.5, y));
        }

    } else if (id == "maps") {
        // Cartographie : grille 3×3 type heatmap, deux cases « chaudes ».
        const QRectF g(4, 4, 16, 16);
        const qreal s = g.width() / 3.0;
        p.drawRoundedRect(g, 1.2, 1.2);
        for (int i = 1; i < 3; ++i) {
            p.drawLine(QPointF(g.left() + i * s, g.top()),
                       QPointF(g.left() + i * s, g.bottom()));
            p.drawLine(QPointF(g.left(), g.top() + i * s),
                       QPointF(g.right(), g.top() + i * s));
        }
        p.save();
        QColor fill = p.pen().color();
        fill.setAlphaF(0.45f);
        p.setBrush(fill);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(g.left() + 2 * s, g.top(), s, s));         // haut-droite
        p.drawRect(QRectF(g.left() + s, g.top() + s, s, s));         // centre
        p.restore();

    } else if (id == "damos") {
        // Éditeur de recettes : crayon.
        QPainterPath body;
        body.moveTo(6, 20);     // pointe
        body.lineTo(8, 15);
        body.lineTo(15, 8);
        body.lineTo(17, 10);
        body.lineTo(10, 17);
        body.closeSubpath();
        p.drawPath(body);
        p.drawLine(QPointF(8, 15), QPointF(10, 17));   // séparation mine/bois

    } else if (id == "3d") {
        // Cube isométrique (visualisation 3D des maps).
        QPainterPath top;
        top.moveTo(12, 3);
        top.lineTo(20, 7);
        top.lineTo(12, 11);
        top.lineTo(4, 7);
        top.closeSubpath();
        p.drawPath(top);
        p.drawLine(QPointF(4, 7), QPointF(4, 15));
        p.drawLine(QPointF(20, 7), QPointF(20, 15));
        p.drawLine(QPointF(4, 15), QPointF(12, 19));
        p.drawLine(QPointF(20, 15), QPointF(12, 19));
        p.drawLine(QPointF(12, 11), QPointF(12, 19));

    } else if (id == "automods") {
        // Engrenage (réglages automatiques).
        const QPointF c(12, 12);
        const qreal rIn = 5.0, rTooth = 7.2;
        for (int a = 0; a < 360; a += 45) {
            const qreal rad = qDegreesToRadians(static_cast<double>(a));
            p.drawLine(QPointF(c.x() + rIn * qCos(rad), c.y() + rIn * qSin(rad)),
                       QPointF(c.x() + rTooth * qCos(rad), c.y() + rTooth * qSin(rad)));
        }
        p.drawEllipse(c, rIn, rIn);
        p.drawEllipse(c, 1.9, 1.9);

    } else if (id == "checksum") {
        // Bouclier + coche (intégrité / checksum).
        QPainterPath sh;
        sh.moveTo(12, 3);
        sh.lineTo(19, 6);
        sh.lineTo(18, 13);
        sh.lineTo(12, 21);
        sh.lineTo(6, 13);
        sh.lineTo(5, 6);
        sh.closeSubpath();
        p.drawPath(sh);
        QPainterPath check;
        check.moveTo(8.8, 12);
        check.lineTo(11.2, 14.8);
        check.lineTo(15.6, 9);
        p.drawPath(check);

    } else if (id == "compare") {
        // Deux panneaux superposés (comparaison de ROMs).
        p.drawRoundedRect(QRectF(4, 4, 12, 12), 1.6, 1.6);
        p.drawRoundedRect(QRectF(8, 8, 12, 12), 1.6, 1.6);

    } else if (id == "versions") {
        // Branche git : tronc + bifurcation.
        p.drawLine(QPointF(8, 5), QPointF(8, 19));
        QPainterPath branch;
        branch.moveTo(8, 12);
        branch.cubicTo(8, 8.5, 12, 8.5, 16, 8);
        p.drawPath(branch);
        drawDot(p, 8, 5.5, 2.0);
        drawDot(p, 8, 18.5, 2.0);
        drawDot(p, 16.5, 7.5, 2.0);

    } else if (id == "a2l") {
        // Document A2L : page + lettre « A ».
        p.drawRoundedRect(QRectF(5, 3, 14, 18), 1.6, 1.6);
        QPainterPath a;
        a.moveTo(9, 16.5);
        a.lineTo(12, 7.5);
        a.lineTo(15, 16.5);
        p.drawPath(a);
        p.drawLine(QPointF(10.2, 13), QPointF(13.8, 13));

    } else if (id == "obd") {
        // Cadran live (OBD datalog) : demi-cercle gradué + aiguille.
        p.drawArc(QRectF(3, 7, 18, 18), 0, 180 * 16);
        p.drawLine(QPointF(3, 16), QPointF(21, 16));
        p.drawLine(QPointF(12, 16), QPointF(16.5, 10.5));   // aiguille
        drawDot(p, 12, 16, 1.6);
        p.drawLine(QPointF(6, 12.5), QPointF(7.3, 13.4));   // ticks
        p.drawLine(QPointF(18, 12.5), QPointF(16.7, 13.4));

    } else if (id == "can") {
        // Bus CAN : ligne + nœuds.
        p.drawLine(QPointF(3, 9), QPointF(21, 9));
        for (qreal x : {7.0, 12.0, 17.0}) {
            p.drawLine(QPointF(x, 9), QPointF(x, 13));
            p.drawRoundedRect(QRectF(x - 1.6, 13, 3.2, 3.2), 0.6, 0.6);
        }

    } else if (id == "library") {
        // Bibliothèque : pile de livres (3 droits + 1 incliné).
        p.drawRoundedRect(QRectF(4.5, 8, 3, 12), 0.6, 0.6);
        p.drawRoundedRect(QRectF(8, 6, 3, 14), 0.6, 0.6);
        p.drawRoundedRect(QRectF(11.5, 9, 3, 11), 0.6, 0.6);
        p.drawLine(QPointF(4.5, 11), QPointF(7.5, 11));
        p.drawLine(QPointF(8, 9), QPointF(11, 9));
        p.drawLine(QPointF(11.5, 12), QPointF(14.5, 12));
        p.save();
        p.translate(18.2, 20);
        p.rotate(-20);
        p.drawRoundedRect(QRectF(0, -11, 3, 11), 0.6, 0.6);
        p.restore();
    }
}

QPixmap renderPixmap(const QString& id, const QColor& color, int px) {
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(px / 24.0, px / 24.0);
    QPen pen(color);
    pen.setWidthF(1.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    paintIcon(p, id);
    p.end();
    return pm;
}

} // namespace

QIcon navIcon(const QString& id) {
    static const QStringList known = {
        "hub", "project", "mpps", "hex", "maps", "damos", "3d",
        "automods", "checksum", "compare", "versions", "a2l", "can", "library", "obd"
    };
    if (!known.contains(id)) return {};

    const QColor rest(0xa9, 0xb2, 0xc6);    // gris clair (repos)
    const QColor active(0xa5, 0xb4, 0xfc);  // indigo (sélectionné / survol)
    const int px = 48;                      // rendu HiDPI, réduit par le bouton

    QIcon ic;
    ic.addPixmap(renderPixmap(id, rest,   px), QIcon::Normal, QIcon::Off);
    ic.addPixmap(renderPixmap(id, active, px), QIcon::Normal, QIcon::On);
    ic.addPixmap(renderPixmap(id, active, px), QIcon::Active, QIcon::Off);
    return ic;
}

} // namespace ecu_studio
