#pragma once
#include <QWidget>
#include <QString>
#include <cstdint>
#include <vector>

class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QPushButton;
class QDoubleSpinBox;

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

signals:
    // Émis quand l'utilisateur demande à voir la map sélectionnée dans le panel Hex.
    void gotoAddressRequested(quint32 address);

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
    };

    void buildUi();
    void refreshMaps();           // reconstruit la liste à partir du catalogue
    void rebuildMapTable();       // remplit la table des maps
    void onMapSelectionChanged();
    void loadGrid(quint32 address);
    void onCellChanged(QTableWidgetItem* item);
    void applyPercent();
    void applyFullStage1();
    void gotoHex();
    void setStatus(const QString& msg, bool error = false);

    RomDocument* m_doc = nullptr;

    QTableWidget*   m_mapTable   = nullptr;  // liste des maps
    QTableWidget*   m_grid       = nullptr;  // cellules de la map sélectionnée
    QLabel*         m_infoLabel  = nullptr;
    QLabel*         m_statusLabel = nullptr;
    QDoubleSpinBox* m_pctSpin    = nullptr;
    QPushButton*    m_applyPctBtn = nullptr;
    QPushButton*    m_applyStage1Btn = nullptr;
    QPushButton*    m_gotoHexBtn = nullptr;
    QPushButton*    m_openDamosBtn = nullptr;

    std::vector<MapEntry> m_entries;
    int       m_currentRow  = -1;   // ligne sélectionnée dans m_mapTable
    quint32   m_currentAddr = 0;    // adresse de la map affichée dans la grille
    std::size_t m_dataOff   = 0;    // offset ROM du bloc de données de la map
    int       m_curNx       = 0;
    int       m_curNy       = 0;
    bool      m_loadingGrid = false; // garde-fou contre onCellChanged pendant le remplissage
};

} // namespace ecu_studio
