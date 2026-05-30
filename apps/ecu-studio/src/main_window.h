#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <memory>

// Réutilise SidebarNav de SocketSpy directement
#include "sidebar_nav.h"

namespace ecu_studio {

class MppsPanel;
class HexViewPanel;
class MapEditorPanel;
class ProjectPanel;
class AutoModsPanel;
class ChecksumPanel;
class ComparePanel;
class GitPanel;
class A2lPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void setupUi();
    void setupMenuBar();
    void setupStatusBar();

    socketspy::gui::SidebarNav* m_sidebar{nullptr};

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
