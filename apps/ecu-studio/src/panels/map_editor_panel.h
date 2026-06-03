#pragma once
#include <QWidget>
#include <QString>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QPushButton;
class QDoubleSpinBox;
class QLineEdit;
class QCheckBox;

namespace ecu_studio {

class RomDocument;

// Panneau d'édition des maps : liste les maps connues (catalogue Stage 1) et
// les maps détectées par heuristique (ecu::findMaps), affiche la grille
// éditable de la map sélectionnée et applique les modifications Stage 1.
class MapEditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit MapEditorPanel(RomDocument* doc, QWidget* parent = nullptr);

public slots:
    // Lance la recherche heuristique de maps sur la ROM courante (menu Outils).
    void runMapFinder();
    // Lance open_damos : relocalise automatiquement les maps connues par empreinte
    // d'axe dans n'importe quelle ROM EDC16C34 PSA (pas de DAMOS dédié requis).
    void runOpenDamos();
    // Importe un fichier open_damos.json externe (recipe custom).
    void importOpenDamosRecipe();
    // Reconstruit la liste à partir du catalogue / recipe — appelé après une
    // sauvegarde via l'éditeur DAMOS pour refléter les changements.
    void refreshFromCatalog() { refreshMaps(); }

signals:
    // Émis quand l'utilisateur demande à voir la map sélectionnée dans le panel Hex.
    void gotoAddressRequested(quint32 address);
    // Émis quand l'utilisateur demande la visualisation 3D de la map sélectionnée.
    // Transmet les unités physiques (axe X/Y et data) issues du recipe open_damos.
    void view3dRequested(quint32 address, const QString& name,
                         const QString& xUnit, const QString& yUnit,
                         const QString& dataUnit);

private:
    // Origine d'une entrée listée dans la table des maps.
    struct MapEntry {
        QString     name;
        quint32     address = 0;
        int         nx      = 0;
        int         ny      = 0;
        double      score   = 0.0;   // -1 => pas de score (map connue)
        bool        stage1  = false; // provient du catalogue Stage 1
        int         defaultPct = 0;  // pourcentage Stage 1 par défaut
        bool        openDamos  = false; // relocalisée par open_damos
        bool        fallback   = false; // adresse par défaut (pas d'empreinte)
        QString     matchInfo;          // mode de match / source (open_damos)
        // Conversion phys↔raw depuis open_damos (data.factor / data.offset)
        double      factor = 1.0;
        double      offset = 0.0;
        bool        hasConversion = false;
        // Valeurs stock issues du recipe (optionnelles)
        std::optional<int64_t> stockRaw;
        std::optional<double>  stockPhys;
        std::string            unit;        // unité physique des cellules (Nm, mg/cyc…)
        std::string            xAxisUnit;   // unité axe X (rpm, %…)
        std::string            yAxisUnit;   // unité axe Y
        std::string            description;
    };

    void buildUi();
    void refreshMaps();           // reconstruit la liste à partir du catalogue
    void rebuildMapTable();       // remplit la table des maps
    void applyMapFilter();        // filtre la liste des maps (search box)
    // Dialog de sélection de baseline pour le mode fantôme : commit git ou
    // fichier .bin. Met à jour m_doc->setBaselineFromBytes.
    void pickBaseline();
    void onMapSelectionChanged();
    void loadGrid(quint32 address);
    void onCellChanged(QTableWidgetItem* item);
    void applyPercent();
    void applyFullStage1();
    void gotoHex();
    void view3d();
    void setStatus(const QString& msg, bool error = false);

    // Heatmap : recolore toutes les cellules selon leur valeur (froid→chaud).
    void recolorHeatmap();
    void onGridSelectionChanged();   // met à jour le readout min/max/moy

    // Opérations sur la sélection multi-cellules.
    void opSet();          // fixe une valeur
    void opAdd();          // ajoute une valeur brute
    void opMultiply();     // multiplie par (1 + %/100)
    void opInterpolate();  // interpolation linéaire bord-à-bord (lignes)
    void opSmooth();       // lissage 3×3 (moyenne des voisins)
    // Applique f(valeur) à chaque cellule sélectionnée et persiste dans la ROM.
    void applyToSelection(const QString& label,
                          const std::function<double(int, double)>& fn);

    // Copier / coller d'une région (TSV, compatible Excel).
    void copyRegionTsv();
    void pasteRegionTsv();

    RomDocument* m_doc = nullptr;

    QLineEdit*      m_mapFilter  = nullptr;  // search box sur la liste
    QTableWidget*   m_mapTable   = nullptr;  // liste des maps
    QTableWidget*   m_grid       = nullptr;  // cellules de la map sélectionnée
    QLabel*         m_infoLabel  = nullptr;
    QLabel*         m_selLabel   = nullptr;  // readout min/max/moy de la sélection
    QLabel*         m_statusLabel = nullptr;
    QCheckBox*      m_heatmapChk = nullptr;
    QCheckBox*      m_ghostChk   = nullptr;  // mode fantôme : superpose baseline
    QPushButton*    m_baselineBtn = nullptr; // dialog "Baseline…" (commit git)
    QDoubleSpinBox* m_pctSpin    = nullptr;
    QPushButton*    m_applyPctBtn = nullptr;
    QPushButton*    m_applyStage1Btn = nullptr;
    QPushButton*    m_gotoHexBtn = nullptr;
    QPushButton*    m_view3dBtn  = nullptr;
    QPushButton*    m_openDamosBtn     = nullptr;
    QPushButton*    m_importRecipeBtn  = nullptr;

    std::vector<MapEntry> m_entries;
    int       m_currentRow  = -1;   // ligne sélectionnée dans m_mapTable
    quint32   m_currentAddr = 0;    // adresse de la map affichée dans la grille
    std::size_t m_dataOff   = 0;    // offset ROM du bloc de données de la map
    int       m_curNx       = 0;
    int       m_curNy       = 0;
    bool      m_loadingGrid = false; // garde-fou contre onCellChanged pendant le remplissage
    // Taille de cellule choisie par l'utilisateur (drag des en-têtes) — persistante
    // entre les sélections de map.
    int       m_colWidth    = 60;
    int       m_rowHeight   = 24;
    bool      m_applyingCellSize = false; // évite la boucle sectionResized → setSection
};

} // namespace ecu_studio
