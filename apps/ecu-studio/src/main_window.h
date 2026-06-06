#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QTranslator>
#include <memory>

// Réutilise SidebarNav de SocketSpy directement
#include "sidebar_nav.h"
#include "ecu/ProjectManager.hpp"

namespace ecu_studio {

class RomDocument;
class MppsPanel;
class HexViewPanel;
class MapEditorPanel;
class DamosEditorPanel;
class Map3dPanel;
class ProjectPanel;
class AutoModsPanel;
class ChecksumPanel;
class ComparePanel;
class GitPanel;
class A2lPanel;
class CanPanel;
class OpenDamosLibraryPanel;
class HubLauncherPanel;
class Updater;
class WelcomeScreen;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Affiche l'écran d'accueil (premier lancement et menu Aide).
    void showWelcome();

protected:
    void closeEvent(QCloseEvent* e) override;
    // Glisser-déposer d'un fichier ROM sur la fenêtre → ouverture directe.
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void setupUi();
    // Construit un panneau « Bientôt disponible » (utilisé pour MPPS).
    QWidget* makeComingSoonPanel(const QString& title, const QString& subtitle);
    // Charge une ROM depuis un chemin (route .ols/.zip/.hex via WinolsParser,
    // sinon ouverture binaire directe). Utilisé par le drag-drop.
    void loadRomPath(const QString& path);
    void importWinolsFile(const QString& path);
    void setupMenuBar();
    void setupToolBar();   // flèches annuler/rétablir (Ctrl+Z / Ctrl+Y, via git)
    void setupStatusBar();
    void wirePanels();   // câble les signaux inter-panels (goto adresse, ROM lue…)
    void connectWelcomeSignals();

    // Persistance d'état de la fenêtre (géométrie, panneau courant, dernier
    // projet/ROM) via QSettings.
    void saveWindowState();
    void restoreWindowState();

    // Met à jour la barre de statut depuis l'état du document ROM.
    void updateRomStatus();

    // Menu « Projets récents » : reconstruit la liste et applique l'ouverture.
    void rebuildRecentMenu();
    void openProjectById(const QString& id);
    void openRomFromDialog();

    void importWinols();    // importe un export WinOLS (.zip/.hex/.bin) dans le document
    void generateReport();  // génère un rapport HTML des modifications de la ROM
    void showAbout();       // dialogue « À propos »

    // Lance le dialogue de mise à jour. Si silent=true, ne montre rien tant
    // qu'aucune mise à jour n'est disponible (vérification au démarrage).
    void checkForUpdates(bool silent);

    // Applique la langue (code "fr" ou "en"), persiste dans QSettings et
    // reinstalle le traducteur Qt. Recrée l'écran d'accueil si nécessaire.
    void applyLanguage(const QString& code);
    void recreateWelcomeScreen();
    void autoSave();

    socketspy::gui::SidebarNav* m_sidebar{nullptr};
    Updater* m_updater{nullptr};
    WelcomeScreen* m_welcomeScreen{nullptr};
    std::unique_ptr<QTranslator> m_translator;
    QTimer* m_autosaveTimer{nullptr};

    // Gestionnaire de projets (pour ouvrir un projet récent directement, sans
    // passer par le panneau Projet).
    std::unique_ptr<ecu::ProjectManager> m_projects;
    QMenu* m_recentMenu{nullptr};

    // Document partagé : la ROM actuellement chargée, référencée par tous les panels.
    RomDocument* m_doc{nullptr};

    // Panels ECU Studio
    MppsPanel*      m_mppsPanel{nullptr};
    QWidget*        m_mppsComingSoon{nullptr};  // placeholder « bientôt » (sidebar)
    HexViewPanel*   m_hexPanel{nullptr};
    MapEditorPanel*   m_mapEditor{nullptr};
    DamosEditorPanel* m_damosEditor{nullptr};
    Map3dPanel*       m_map3dPanel{nullptr};
    ProjectPanel*   m_projectPanel{nullptr};
    AutoModsPanel*  m_autoMods{nullptr};
    ChecksumPanel*  m_checksumPanel{nullptr};
    ComparePanel*   m_comparePanel{nullptr};
    GitPanel*       m_gitPanel{nullptr};
    A2lPanel*       m_a2lPanel{nullptr};
    CanPanel*       m_canPanel{nullptr};
    OpenDamosLibraryPanel* m_libraryPanel{nullptr};
    HubLauncherPanel* m_hubPanel{nullptr};

    // Barre de statut
    QLabel* m_romLabel{nullptr};      // nom + taille de la ROM
    QLabel* m_ecuLabel{nullptr};      // ECU courant
    QLabel* m_relocBadge{nullptr};    // badge qualité de relocalisation OpenDAMOS
    QLabel* m_modifiedLabel{nullptr}; // indicateur ● de modification
    QLabel* m_deviceLabel{nullptr};   // statut connexion MPPS
};

} // namespace ecu_studio
