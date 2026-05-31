#include "map3d_view.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPolygonF>
#include <QPointF>
#include <QColor>
#include <QFont>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ecu_studio {

namespace {

// Gradient froid → chaud (bleu → cyan → vert → jaune → rouge) pour t ∈ [0,1].
QColor heatColor(double t) {
    t = std::clamp(t, 0.0, 1.0);
    // 5 paliers interpolés linéairement.
    struct Stop { double pos; int r, g, b; };
    static const Stop stops[] = {
        {0.00,  20,  40, 120},   // bleu profond (froid)
        {0.25,  30, 140, 200},   // cyan
        {0.50,  40, 190,  90},   // vert
        {0.75, 240, 200,  40},   // jaune
        {1.00, 220,  40,  40},   // rouge (chaud)
    };
    for (int i = 1; i < 5; ++i) {
        if (t <= stops[i].pos) {
            const double span = stops[i].pos - stops[i - 1].pos;
            const double f    = span > 0 ? (t - stops[i - 1].pos) / span : 0.0;
            const int r = static_cast<int>(stops[i - 1].r + f * (stops[i].r - stops[i - 1].r));
            const int g = static_cast<int>(stops[i - 1].g + f * (stops[i].g - stops[i - 1].g));
            const int b = static_cast<int>(stops[i - 1].b + f * (stops[i].b - stops[i - 1].b));
            return QColor(r, g, b);
        }
    }
    return QColor(stops[4].r, stops[4].g, stops[4].b);
}

} // namespace

Map3dViewPainter::Map3dViewPainter(QWidget* parent) : QWidget(parent) {
    setMinimumSize(360, 300);
    setMouseTracking(false);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor("#0f1520"));
    setPalette(pal);
}

void Map3dViewPainter::setSurface(const SurfaceData& data) {
    m_data    = data;
    m_hasData = (data.nx > 0 && data.ny > 0 &&
                 data.z.size() == static_cast<std::size_t>(data.nx) *
                                  static_cast<std::size_t>(data.ny));
    m_zMin =  std::numeric_limits<double>::max();
    m_zMax = -std::numeric_limits<double>::max();
    if (m_hasData) {
        for (double v : m_data.z) {
            m_zMin = std::min(m_zMin, v);
            m_zMax = std::max(m_zMax, v);
        }
    } else {
        m_zMin = m_zMax = 0.0;
    }
    update();
}

void Map3dViewPainter::clearSurface() {
    m_hasData = false;
    m_data = {};
    update();
}

void Map3dViewPainter::setHeatmap(bool on) {
    m_heatmap = on;
    update();
}

void Map3dViewPainter::mousePressEvent(QMouseEvent* e) {
    m_lastPos  = e->pos();
    m_dragging = true;
}

void Map3dViewPainter::mouseMoveEvent(QMouseEvent* e) {
    if (!m_dragging || m_heatmap) return;
    const QPoint d = e->pos() - m_lastPos;
    m_lastPos = e->pos();
    m_yaw   += d.x() * 0.5;
    m_pitch  = std::clamp(m_pitch + d.y() * 0.4, 5.0, 85.0);
    update();
}

void Map3dViewPainter::wheelEvent(QWheelEvent* e) {
    const double step = e->angleDelta().y() > 0 ? 1.1 : (1.0 / 1.1);
    m_zoom = std::clamp(m_zoom * step, 0.4, 4.0);
    update();
}

void Map3dViewPainter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor("#0f1520"));

    if (!m_hasData) { paintEmpty(p); return; }
    if (m_heatmap)  { paintHeatmap(p); return; }
    paint3d(p);
}

void Map3dViewPainter::paintEmpty(QPainter& p) {
    p.setPen(QColor("#7c8fa6"));
    QFont f = p.font();
    f.setPointSize(11);
    p.setFont(f);
    p.drawText(rect(), Qt::AlignCenter,
               tr("Aucune map à afficher.\nSélectionnez une map ci-dessus."));
}

// ── Rendu pseudo-3D (projection isométrique d'un maillage) ────────────────────
void Map3dViewPainter::paint3d(QPainter& p) {
    const int nx = m_data.nx, ny = m_data.ny;
    const double zRange = (m_zMax - m_zMin);

    // Projette une coordonnée logique (gx ∈ [0,nx-1], gy ∈ [0,ny-1], val) en 2D.
    const double cx = width()  * 0.5;
    const double cy = height() * 0.55;
    const double scale = std::min(width(), height()) * 0.012 * m_zoom;
    const double yawR   = m_yaw   * M_PI / 180.0;
    const double pitchR = m_pitch * M_PI / 180.0;
    const double cosY = std::cos(yawR),  sinY = std::sin(yawR);
    const double sinP = std::sin(pitchR);

    const double spanX = std::max(1, nx - 1);
    const double spanY = std::max(1, ny - 1);
    const double zHeight = std::min(width(), height()) * 0.35 * m_zoom;

    auto project = [&](double gx, double gy, double val) -> QPointF {
        // Centre la grille puis met à l'échelle ~[-1,1].
        const double x = (gx / spanX - 0.5) * spanX;
        const double y = (gy / spanY - 0.5) * spanY;
        // Rotation horizontale (yaw) autour de l'axe vertical.
        const double rx = x * cosY - y * sinY;
        const double ry = x * sinY + y * cosY;
        // Hauteur normalisée.
        const double t = zRange > 0 ? (val - m_zMin) / zRange : 0.0;
        const double sx = cx + rx * scale * 6.0;
        const double sy = cy + ry * scale * sinP * 6.0 - t * zHeight;
        return QPointF(sx, sy);
    };

    auto zAt = [&](int gx, int gy) -> double {
        const std::size_t idx = static_cast<std::size_t>(gy) *
                                static_cast<std::size_t>(nx) +
                                static_cast<std::size_t>(gx);
        return m_data.z[idx];
    };
    auto depthAt = [&](double gx, double gy) {
        const double x = (gx / spanX - 0.5) * spanX;
        const double y = (gy / spanY - 0.5) * spanY;
        return x * sinY + y * cosY;  // ry : plus grand = plus proche
    };

    // Dessine les quads de l'arrière vers l'avant (painter's algorithm) en
    // triant par profondeur. Chaque quad coloré par sa valeur moyenne.
    struct Quad { QPolygonF poly; double depth; double t; };
    std::vector<Quad> quads;
    if (nx > 1 && ny > 1)
        quads.reserve(static_cast<std::size_t>(nx - 1) *
                      static_cast<std::size_t>(ny - 1));

    for (int gy = 0; gy + 1 < ny; ++gy) {
        for (int gx = 0; gx + 1 < nx; ++gx) {
            QPolygonF poly;
            poly << project(gx,     gy,     zAt(gx,     gy))
                 << project(gx + 1, gy,     zAt(gx + 1, gy))
                 << project(gx + 1, gy + 1, zAt(gx + 1, gy + 1))
                 << project(gx,     gy + 1, zAt(gx,     gy + 1));
            const double avg = 0.25 * (zAt(gx, gy) + zAt(gx + 1, gy) +
                                       zAt(gx + 1, gy + 1) + zAt(gx, gy + 1));
            const double t = zRange > 0 ? (avg - m_zMin) / zRange : 0.0;
            quads.push_back({ std::move(poly), depthAt(gx + 0.5, gy + 0.5), t });
        }
    }
    std::sort(quads.begin(), quads.end(),
              [](const Quad& a, const Quad& b) { return a.depth < b.depth; });

    QPen edge(QColor(255, 255, 255, 40));
    edge.setWidthF(0.6);
    for (const auto& q : quads) {
        p.setPen(edge);
        p.setBrush(heatColor(q.t));
        p.drawPolygon(q.poly);
    }

    // Titre + plage de valeurs + repère d'interaction.
    p.setPen(QColor("#e6edf3"));
    QFont tf = p.font();
    tf.setPointSize(10);
    tf.setBold(true);
    p.setFont(tf);
    p.drawText(QRectF(8, 6, width() - 16, 20), Qt::AlignLeft, m_data.title);

    p.setPen(QColor("#7c8fa6"));
    QFont sf = p.font();
    sf.setBold(false);
    sf.setPointSize(9);
    p.setFont(sf);
    p.drawText(QRectF(8, height() - 22, width() - 16, 18), Qt::AlignLeft,
               tr("min %1   max %2   —   glisser : rotation, molette : zoom")
                   .arg(m_zMin, 0, 'f', 0).arg(m_zMax, 0, 'f', 0));
}

// ── Heatmap 2D (vue de dessus) ────────────────────────────────────────────────
void Map3dViewPainter::paintHeatmap(QPainter& p) {
    const int nx = m_data.nx, ny = m_data.ny;
    const double zRange = (m_zMax - m_zMin);

    const int marginL = 44, marginB = 28, marginT = 26, marginR = 12;
    const QRectF area(marginL, marginT,
                      std::max(1, width() - marginL - marginR),
                      std::max(1, height() - marginT - marginB));
    const double cw = area.width()  / nx;
    const double ch = area.height() / ny;

    for (int gy = 0; gy < ny; ++gy) {
        for (int gx = 0; gx < nx; ++gx) {
            const std::size_t idx = static_cast<std::size_t>(gy) *
                                    static_cast<std::size_t>(nx) +
                                    static_cast<std::size_t>(gx);
            const double v = m_data.z[idx];
            const double t = zRange > 0 ? (v - m_zMin) / zRange : 0.0;
            // gy=0 en bas (axe Y croissant vers le haut).
            const QRectF cell(area.left() + gx * cw,
                              area.top() + (ny - 1 - gy) * ch, cw + 0.5, ch + 0.5);
            p.fillRect(cell, heatColor(t));
        }
    }

    p.setPen(QColor("#e6edf3"));
    QFont tf = p.font();
    tf.setPointSize(10);
    tf.setBold(true);
    p.setFont(tf);
    p.drawText(QRectF(8, 4, width() - 16, 18), Qt::AlignLeft, m_data.title);

    // Libellés d'axes (premier/milieu/dernier pour rester lisible).
    p.setPen(QColor("#7c8fa6"));
    QFont sf = p.font();
    sf.setBold(false);
    sf.setPointSize(8);
    p.setFont(sf);
    auto label = [](const QStringList& l, int i) {
        return (i >= 0 && i < l.size()) ? l.at(i) : QString();
    };
    if (!m_data.xLabels.isEmpty()) {
        p.drawText(QRectF(area.left(), area.bottom() + 2, 60, marginB),
                   Qt::AlignLeft, label(m_data.xLabels, 0));
        p.drawText(QRectF(area.right() - 60, area.bottom() + 2, 60, marginB),
                   Qt::AlignRight, label(m_data.xLabels, nx - 1));
    }
    if (!m_data.yLabels.isEmpty()) {
        p.drawText(QRectF(0, area.top() - 6, marginL - 4, 16),
                   Qt::AlignRight, label(m_data.yLabels, ny - 1));
        p.drawText(QRectF(0, area.bottom() - 10, marginL - 4, 16),
                   Qt::AlignRight, label(m_data.yLabels, 0));
    }
}

} // namespace ecu_studio
