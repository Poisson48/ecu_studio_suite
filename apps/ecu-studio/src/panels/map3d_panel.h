#pragma once
#include <QWidget>
#include <QString>
#include <cstdint>
#include <vector>

class QComboBox;
class QPushButton;
class QLabel;
class QCheckBox;

namespace ecu_studio {

class RomDocument;
struct SurfaceData;

#ifdef ECU_HAVE_DATAVIZ
class Q3DSurface;
class QSurface3DSeries;
#endif

// Panneau « 3D » : rend la map ECU sélectionnée sous forme de surface 3D
// interactive (rotation/zoom), à la manière de WinOLS, avec un repli heatmap 2D.
//
// Backend de rendu choisi à la compilation :
//   - Qt6::DataVisualization (Q3DSurface) si le module est disponible
//     (macro ECU_HAVE_DATAVIZ définie par CMake) ;
//   - sinon un rendu QPainter pseudo-3D / heatmap maison, sans dépendance
//     supplémentaire, qui fonctionne partout.
//
// Le panneau est autonome : il liste les maps Stage 1 du catalogue pour l'ECU
// du document et permet une recherche heuristique (ecu::findMaps). main_window
// peut aussi pousser une map précise via le slot showMap().
class Map3dPanel : public QWidget {
    Q_OBJECT
public:
    explicit Map3dPanel(RomDocument* doc, QWidget* parent = nullptr);

public slots:
    // Affiche en 3D la map située à l'adresse donnée (appelé depuis MapEditor).
    // Les unités (axes X/Y, data) viennent du recipe open_damos quand connues —
    // elles s'affichent sur les axes et le bandeau info.
    void showMap(quint32 address,
                 const QString& name    = {},
                 const QString& xUnit   = {},
                 const QString& yUnit   = {},
                 const QString& dataUnit = {});

private:
    // Une entrée listée dans le sélecteur de maps.
    struct MapEntry {
        QString name;
        quint32 address = 0;
        int     nx      = 0;
        int     ny      = 0;
        bool    stage1  = false;
        QString xUnit;     // unité axe X (rpm, kg/h, …) — depuis open_damos
        QString yUnit;     // unité axe Y
        QString dataUnit;  // unité des valeurs (Nm, mg/cyc, hPa, …)
    };

    void buildUi();
    void refreshMaps();            // reconstruit la liste depuis le catalogue
    void rebuildCombo();           // remplit le sélecteur
    void onMapSelected(int index); // charge la map choisie
    void searchMaps();             // recherche heuristique (ecu::findMaps)
    void reloadCurrent();          // relit la map courante (romModified)
    void render(quint32 address);  // lit la map et la transmet à la vue
    void toggleHeatmap(bool on);
    void setStatus(const QString& msg, bool error = false);
    // Édition d'une cellule depuis le clic dans la vue 3D — ouvre une boîte
    // de dialogue, écrit en ROM via writeSwordBE, relit et notifie le doc.
    void onCellClicked(int gx, int gy, double currentValue);

    // Adaptateurs vers le backend de rendu effectif (cf. map3d_panel.cpp).
    void viewSetSurface(const SurfaceData& data);
    void viewSetHeatmap(bool on);
    void viewClear();

    RomDocument* m_doc = nullptr;

    QComboBox*   m_mapCombo  = nullptr;
    QPushButton* m_searchBtn = nullptr;
    QCheckBox*   m_heatChk   = nullptr;
    QCheckBox*   m_ghostChk  = nullptr;  // mode fantôme (baseline overlay)
    QLabel*      m_infoLabel = nullptr;
    QLabel*      m_statusLabel = nullptr;
    QWidget*     m_view      = nullptr;  // Map3dViewPainter ou conteneur Q3DSurface

#ifdef ECU_HAVE_DATAVIZ
    // Handles du backend Qt6 DataVisualization (renseignés par buildUi()).
    Q3DSurface*       m_surface = nullptr;
    QSurface3DSeries* m_series  = nullptr;
#endif

    std::vector<MapEntry> m_entries;
    quint32 m_currentAddr = 0;  // adresse de la map affichée (0 = aucune)
};

} // namespace ecu_studio
