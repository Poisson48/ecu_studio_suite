#pragma once
#include <QString>
#include <QWidget>

#include "ecu/OpenDamos.hpp"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QTextEdit;

namespace ecu_studio {

class RomDocument;

// Éditeur DAMOS — modifie ou crée un recipe open_damos.json depuis l'app.
//
// Use cases :
//   - ouvrir un recipe existant (ressources/<ecu>/open_damos.json),
//   - démarrer un nouveau recipe quand on attaque un ECU inconnu,
//   - ajouter/supprimer/dupliquer une characteristic,
//   - détecter automatiquement des candidates via ecu::findMaps,
//   - capturer l'empreinte d'axe à l'adresse courante depuis la ROM,
//   - sauvegarder vers le chemin ressources/ par défaut ou Save As.
//
// Le panneau s'adosse à RomDocument pour récupérer la ROM courante et l'ECU id
// (utilisés par la détection et la capture d'empreinte).
class DamosEditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit DamosEditorPanel(RomDocument* doc, QWidget* parent = nullptr);

    // Charge un recipe en mémoire (ne touche pas au disque).
    void setRecipe(ecu::DamosRecipe recipe, const QString& sourcePath = {});

    // Démarre un recipe vide pour l'ECU donné (proposé sur ECU inconnu).
    void newEmptyRecipe(const QString& ecuId);

public slots:
    void openRecipeFile();        // dialogue d'ouverture
    void saveRecipe();            // écrit vers m_path (ou Save As si vide)
    void saveRecipeAs();
    void onAddEntry();
    void onDeleteEntry();
    void onDuplicateEntry();
    void detectFromRom();         // ecu::findMaps → ajoute les candidates
    void captureFingerprintHere();// lit l'axe à l'adresse courante de la ROM

    // ── AutoMods ─────────────────────────────────────────────────────────
    void onAddAutoMod();
    void onDeleteAutoMod();
    void onDuplicateAutoMod();

signals:
    // Signale au reste de l'app qu'un recipe a été sauvegardé pour cet ECU —
    // MapEditorPanel peut alors rafraîchir ses entrées.
    void recipeSaved(const QString& ecuId, const QString& path);

private:
    void buildUi();
    QWidget* buildAutoModsPage();
    void rebuildEntryTable();
    void rebuildAutoModTable();
    void onEntrySelectionChanged();
    void onAutoModSelectionChanged();
    void writeFormToCurrent();   // pousse le formulaire dans m_recipe.characteristics[idx]
    void loadCurrentToForm();    // peuple le formulaire depuis l'entrée sélectionnée
    void writeAutoModFormToCurrent();
    void loadCurrentAutoModToForm();
    void setStatus(const QString& msg, bool error = false);
    void markDirty(bool dirty = true);
    int  selectedRow() const;
    int  selectedAutoModRow() const;

    // Aide : adresse hex de l'entrée courante (depuis defaultAddress).
    quint32 currentEntryAddress() const;

    RomDocument*       m_doc = nullptr;
    ecu::DamosRecipe   m_recipe;
    QString            m_path;        // chemin disque (vide = jamais sauvé)
    bool               m_dirty = false;
    bool               m_loadingForm = false;

    // ── Toolbar / header ──
    QLineEdit*    m_ecuIdEdit   = nullptr;
    QLabel*       m_pathLabel   = nullptr;
    QPushButton*  m_newBtn      = nullptr;
    QPushButton*  m_openBtn     = nullptr;
    QPushButton*  m_saveBtn     = nullptr;
    QPushButton*  m_saveAsBtn   = nullptr;

    // ── Liste des characteristics ──
    QTableWidget* m_entryTable  = nullptr;
    QPushButton*  m_addBtn      = nullptr;
    QPushButton*  m_dupBtn      = nullptr;
    QPushButton*  m_delBtn      = nullptr;
    QPushButton*  m_detectBtn   = nullptr;

    // ── Formulaire d'édition ──
    QLineEdit*       m_nameEdit       = nullptr;
    QComboBox*       m_typeCombo      = nullptr;
    QLineEdit*       m_categoryEdit   = nullptr;
    QTextEdit*       m_descEdit       = nullptr;
    QLineEdit*       m_addrEdit       = nullptr;
    QSpinBox*        m_nxSpin         = nullptr;
    QSpinBox*        m_nySpin         = nullptr;

    // Axes (jusqu'à 2)
    QComboBox*       m_xAxisTypeCombo = nullptr;
    QLineEdit*       m_xAxisUnitEdit  = nullptr;
    QDoubleSpinBox*  m_xAxisFactorSpin= nullptr;
    QDoubleSpinBox*  m_xAxisOffsetSpin= nullptr;
    QLineEdit*       m_xAxisFpEdit    = nullptr;   // CSV des valeurs
    QPushButton*     m_xAxisCaptureBtn= nullptr;

    QComboBox*       m_yAxisTypeCombo = nullptr;
    QLineEdit*       m_yAxisUnitEdit  = nullptr;
    QDoubleSpinBox*  m_yAxisFactorSpin= nullptr;
    QDoubleSpinBox*  m_yAxisOffsetSpin= nullptr;
    QLineEdit*       m_yAxisFpEdit    = nullptr;
    QPushButton*     m_yAxisCaptureBtn= nullptr;

    // Data
    QComboBox*       m_dataTypeCombo  = nullptr;
    QLineEdit*       m_dataUnitEdit   = nullptr;
    QDoubleSpinBox*  m_dataFactorSpin = nullptr;
    QDoubleSpinBox*  m_dataOffsetSpin = nullptr;

    QLabel*          m_statusLabel    = nullptr;

    // ── AutoMods ─────────────────────────────────────────────────────────
    QTableWidget*    m_autoModTable      = nullptr;
    QPushButton*     m_addAutoModBtn     = nullptr;
    QPushButton*     m_dupAutoModBtn     = nullptr;
    QPushButton*     m_delAutoModBtn     = nullptr;
    QLineEdit*       m_amIdEdit          = nullptr;
    QComboBox*       m_amTypeCombo       = nullptr;   // Pattern / Address
    QLineEdit*       m_amDescEdit        = nullptr;
    QLineEdit*       m_amNoteEdit        = nullptr;
    QLineEdit*       m_amAddressEdit     = nullptr;   // hex
    QLineEdit*       m_amSearchEdit      = nullptr;   // hex bytes
    QLineEdit*       m_amReplaceEdit     = nullptr;   // hex bytes (= bytes pour address)
    QLineEdit*       m_amRestoreEdit     = nullptr;   // hex bytes (optionnel)
    bool             m_loadingAutoMod    = false;
};

} // namespace ecu_studio
