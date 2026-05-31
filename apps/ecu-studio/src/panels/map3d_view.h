#pragma once
#include <QWidget>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <vector>

class QPoint;

namespace ecu_studio {

// Données d'une surface à afficher : grille nx×ny de valeurs + libellés d'axes.
struct SurfaceData {
    int                  nx = 0;
    int                  ny = 0;
    std::vector<double>  z;          // ny*nx valeurs (ligne par ligne)
    QStringList          xLabels;    // nx libellés (axe X)
    QStringList          yLabels;    // ny libellés (axe Y)
    QString              title;
};

// Vue de rendu pseudo-3D / heatmap basée sur QPainter — repli universel sans
// dépendance externe. Rotation à la souris (glisser), zoom à la molette, ou
// bascule en heatmap 2D vue de dessus.
//
// Si Qt6::DataVisualization est disponible (ECU_HAVE_DATAVIZ), Map3dPanel
// instancie à la place un Q3DSurface (cf. map3d_panel.cpp) ; cette vue reste
// le repli compilé partout.
class Map3dViewPainter : public QWidget {
    Q_OBJECT
public:
    explicit Map3dViewPainter(QWidget* parent = nullptr);

    void setSurface(const SurfaceData& data);
    void setHeatmap(bool on);
    void clearSurface();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    void paintEmpty(class QPainter& p);
    void paint3d(class QPainter& p);
    void paintHeatmap(class QPainter& p);

    SurfaceData m_data;
    bool   m_heatmap = false;
    bool   m_hasData = false;
    double m_zMin = 0.0, m_zMax = 0.0;

    // Caméra pseudo-3D.
    double m_yaw   = -35.0;  // degrés, rotation horizontale
    double m_pitch = 28.0;   // degrés, inclinaison
    double m_zoom  = 1.0;
    QPoint m_lastPos;
    bool   m_dragging = false;
};

} // namespace ecu_studio
