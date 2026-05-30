#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <memory>

// Réutilise SidebarNav de SocketSpy directement
#include "sidebar_nav.h"

namespace ecu_studio {

class RomDocument;
class MppsPanel;
class HexViewPanel;
class MapEditorPanel;
class ProjectPanel;
class AutoModsPanel;
class ChecksumPanel;
class ComparePanel;
class GitPanel;
class A2lPanel;
class Updater;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void setupUi();
    void setupMenuBar();
    void setupStatusBar();
    void wirePanels();   // câble les signaux inter-panels (goto adresse, ROM lue…)

    // Lance le dialogue de mise à jour. Si silent=true, ne montre rien tant
    // qu'aucune mise à jour n'est disponible (vérification au démarrage).
    void checkForUpdates(bool silent);

    socketspy::gui::SidebarNav* m_sidebar{nullptr};
    Updater* m_updater{nullptr};

    // Document partagé : la ROM actuellement chargée, référencée par tous les panels.
    RomDocument* m_doc{nullptr};

    // Panels ECU Studio
    MppsPanel*      m_mppsPanel{nullptr};
    HexViewPanel*   m_hexPanel{nullptr};
    MapEditorPanel* m_mapEditor{nullptr};
    ProjectPanel*   m_projectPanel{nullptr};
    AutoModsPanel*  m_autoMods{nullptr};
    ChecksumPanel*  m_checksumPanel{nullptr};
    ComparePanel*   m_comparePanel{nullptr};
    GitPanel*       m_gitPanel{nullptr};
    A2lPanel*       m_a2lPanel{nullptr};

    QLabel* m_statusLabel{nullptr};
    QLabel* m_deviceLabel{nullptr};
};

} // namespace ecu_studio
