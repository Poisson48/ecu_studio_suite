#pragma once
#include <QWidget>
#include <QString>
#include <cstdint>
#include <vector>

#include "map3d_view.h"

class QComboBox;
class QPushButton;
class QLabel;
class QCheckBox;

namespace ecu_studio {

class RomDocument;

#ifdef ECU_HAVE_DATAVIZ
class Q3DSurface;
class QSurface3DSeries;
#endif

// Panneau « 3D » : rend la map ECU sélectionnée sous forme de surface 3D
// interactive (rotation/zoom), à la manière de WinOLS, avec un repli heatmap 2D.
class Map3dPanel : public QWidget {
    Q_OBJECT
public:
    explicit Map3dPanel(RomDocument* doc, QWidget* parent = nullptr);

public slots:
    void showMap(quint32 address,
                 const QString& name    = {},
                 const QString& xUnit   = {},
                 const QString& yUnit   = {},
                 const QString& dataUnit = {});

    void setLiveOperatingPoint(int gx, int gy, double measured, double expected);

private:
    struct MapEntry {
        QString name;
        quint32 address = 0;
        int     nx      = 0;
        int     ny      = 0;
        bool    stage1  = false;
        QString xUnit;
        QString yUnit;
        QString dataUnit;
    };

    void buildUi();
    void refreshMaps();
    void rebuildCombo();
    void onMapSelected(int index);
    void searchMaps();
    void pickBaseline();
    void reloadCurrent();
    void render(quint32 address);
    void toggleHeatmap(bool on);
    void setStatus(const QString& msg, bool error = false);
    void onCellClicked(int gx, int gy, double currentValue);

    void viewSetSurface(const SurfaceData& data);
    void viewSetHeatmap(bool on);
    void viewClear();
    void viewReset();

    RomDocument* m_doc = nullptr;

    QComboBox*   m_mapCombo  = nullptr;
    QComboBox*   m_modeCombo = nullptr;
    QPushButton* m_searchBtn = nullptr;
    QCheckBox*   m_heatChk   = nullptr;
    QCheckBox*   m_ghostChk  = nullptr;
    QPushButton* m_baselineBtn = nullptr;
    QPushButton* m_resetBtn  = nullptr;
    QLabel*      m_infoLabel = nullptr;
    QLabel*      m_statusLabel = nullptr;
    QWidget*     m_view      = nullptr;

#ifdef ECU_HAVE_DATAVIZ
    Q3DSurface*       m_surface = nullptr;
    QSurface3DSeries* m_series  = nullptr;
#endif

    std::vector<MapEntry> m_entries;
    quint32 m_currentAddr = 0;
    SurfaceData m_lastSurface;
};

} // namespace ecu_studio
