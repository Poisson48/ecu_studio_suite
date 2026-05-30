#include "main_window.h"
#include "panels/mpps_panel.h"
#include "panels/hex_view_panel.h"
#include "panels/map_editor_panel.h"
#include "panels/project_panel.h"
#include "panels/automods_panel.h"
#include "panels/checksum_panel.h"
#include "panels/compare_panel.h"
#include "panels/git_panel.h"
#include "panels/a2l_panel.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>

namespace ecu_studio {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setupUi();
    setupMenuBar();
    setupStatusBar();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    setWindowTitle(QString("ECU Studio v%1").arg(APP_VERSION));
    resize(1280, 760);
    setMinimumSize(900, 560);

    // Panels — même pattern que SocketSpy
    m_projectPanel  = new ProjectPanel(this);
    m_mppsPanel     = new MppsPanel(this);
    m_hexPanel      = new HexViewPanel(this);
    m_mapEditor     = new MapEditorPanel(this);
    m_autoMods      = new AutoModsPanel(this);
    m_checksumPanel = new ChecksumPanel(this);
    m_comparePanel  = new ComparePanel(this);
    m_gitPanel      = new GitPanel(this);
    m_a2lPanel      = new A2lPanel(this);

    // Sidebar — réutilise SidebarNav de SocketSpy verbatim
    m_sidebar = new socketspy::gui::SidebarNav(this);
    m_sidebar->addPanel("\xf0\x9f\x93\x81",  tr("Projet"),    m_projectPanel);
    m_sidebar->addPanel("\xf0\x9f\x94\x8c",  tr("MPPS"),      m_mppsPanel);
    m_sidebar->addPanel("\xf0\x9f\x97\x83",  tr("Hex"),       m_hexPanel);
    m_sidebar->addPanel("\xf0\x9f\x93\x8a",  tr("Maps"),      m_mapEditor);
    m_sidebar->addPanel("\xe2\x9a\x99",      tr("AutoMods"),  m_autoMods);
    m_sidebar->addPanel("\xe2\x9c\x85",      tr("Checksum"),  m_checksumPanel);
    m_sidebar->addPanel("\xe2\x89\xa0",      tr("Compare"),   m_comparePanel);
    m_sidebar->addPanel("\xe2\x93\x96",      tr("Git"),       m_gitPanel);
    m_sidebar->addPanel("A2L",               tr("A2L"),       m_a2lPanel);

    auto* central = new QWidget(this);
    auto* hbox    = new QHBoxLayout(central);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(0);
    hbox->addWidget(m_sidebar);
    hbox->addWidget(m_sidebar->stack(), 1);
    setCentralWidget(central);
}

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu(tr("Fichier"));
    fileMenu->addAction(tr("Nouveau projet"),    this, [this]() { m_projectPanel->newProject(); }, QKeySequence::New);
    fileMenu->addAction(tr("Ouvrir projet"),     this, [this]() { m_projectPanel->openProject(); }, QKeySequence::Open);
    fileMenu->addAction(tr("Ouvrir ROM..."),     this, [this]() {
        QString f = QFileDialog::getOpenFileName(this, tr("Ouvrir ROM"), {}, tr("ROM (*.bin *.ori *.mod);;Tous (*.*)"));
        if (!f.isEmpty()) m_hexPanel->loadRom(f);
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Quitter"), this, &QWidget::close, QKeySequence::Quit);

    auto* mppsMenu = menuBar()->addMenu(tr("MPPS"));
    mppsMenu->addAction(tr("Scanner les périphériques"),  m_mppsPanel, &MppsPanel::scanDevices);
    mppsMenu->addAction(tr("Lire ROM"),                   m_mppsPanel, &MppsPanel::readRom);
    mppsMenu->addAction(tr("Écrire ROM"), this, [this]() { m_mppsPanel->writeRom(); });

    auto* toolsMenu = menuBar()->addMenu(tr("Outils"));
    toolsMenu->addAction(tr("Corriger checksums"),  m_checksumPanel, &ChecksumPanel::runCorrection);
    toolsMenu->addAction(tr("Comparer ROMs"),       m_comparePanel,  &ComparePanel::openComparison);
    toolsMenu->addAction(tr("Chercher maps"),       m_mapEditor,     &MapEditorPanel::runMapFinder);
}

void MainWindow::setupStatusBar() {
    m_deviceLabel = new QLabel(tr("Aucun périphérique"), this);
    m_deviceLabel->setObjectName("deviceLabel");
    statusBar()->addPermanentWidget(m_deviceLabel);

    m_statusLabel = new QLabel("ECU Studio prêt", this);
    m_statusLabel->setObjectName("statusLabel");
    statusBar()->addWidget(m_statusLabel);

    // Mettre à jour le label device quand MPPS connecté/déconnecté
    connect(m_mppsPanel, &MppsPanel::deviceStatusChanged, this,
            [this](const QString& status) { m_deviceLabel->setText(status); });
}

} // namespace ecu_studio
