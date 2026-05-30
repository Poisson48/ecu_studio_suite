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
#include "updater.h"
#include "update_dialog.h"
#include "rom_document.h"
#include "ecu/WinolsParser.hpp"

#include <QMenuBar>
#include <QFile>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>
#include <QFileInfo>

namespace ecu_studio {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    m_updater = new Updater(this);
    m_doc     = new RomDocument(this);

    setupUi();
    setupMenuBar();
    setupStatusBar();

    // Vérification silencieuse au démarrage (uniquement en AppImage) — ne
    // dérange l'utilisateur que si une mise à jour est réellement disponible.
    if (m_updater->isAppImage()) {
        QTimer::singleShot(2000, this, [this]() { checkForUpdates(/*silent=*/true); });
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    setWindowTitle(QString("ECU Studio v%1").arg(APP_VERSION));
    resize(1280, 760);
    setMinimumSize(900, 560);

    // Panels — tous reliés au document ROM partagé (sauf MPPS qui le remplit).
    m_projectPanel  = new ProjectPanel(m_doc, this);
    m_mppsPanel     = new MppsPanel(this);
    m_hexPanel      = new HexViewPanel(m_doc, this);
    m_mapEditor     = new MapEditorPanel(m_doc, this);
    m_autoMods      = new AutoModsPanel(m_doc, this);
    m_checksumPanel = new ChecksumPanel(m_doc, this);
    m_comparePanel  = new ComparePanel(m_doc, this);
    m_gitPanel      = new GitPanel(m_doc, this);
    m_a2lPanel      = new A2lPanel(m_doc, this);

    wirePanels();

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

void MainWindow::wirePanels() {
    // La ROM lue par MPPS devient le document courant.
    connect(m_mppsPanel, &MppsPanel::romReadComplete, this,
            [this](QByteArray rom) { m_doc->loadFromData(rom, tr("MPPS")); });

    // Ouvrir un projet définit l'ECU et rafraîchit les auto-mods.
    connect(m_projectPanel, &ProjectPanel::projectOpened, this,
            [this](const QString& ecuId) {
                m_doc->setEcuId(ecuId);
                m_autoMods->refresh();
                // Le dépôt Git du projet est le dossier qui contient la ROM.
                const QString romPath = m_doc->path();
                m_gitPanel->setRepoPath(
                    romPath.isEmpty() ? QString()
                                      : QFileInfo(romPath).absolutePath());
            });

    // « Voir dans Hex » depuis Maps / Compare / A2L → positionne l'éditeur hex.
    auto gotoHex = [this](quint32 address) {
        m_hexPanel->gotoOffset(address);
        m_sidebar->showPanel(m_hexPanel);
    };
    connect(m_mapEditor,    &MapEditorPanel::gotoAddressRequested, this, gotoHex);
    connect(m_comparePanel, &ComparePanel::gotoAddressRequested,   this, gotoHex);
    connect(m_a2lPanel,     &A2lPanel::gotoAddressRequested,       this, gotoHex);
}

void MainWindow::importWinols() {
    const QString f = QFileDialog::getOpenFileName(
        this, tr("Importer un export WinOLS"), {},
        tr("Exports WinOLS (*.zip *.ols *.hex *.bin);;Tous (*.*)"));
    if (f.isEmpty()) return;

    QFile file(f);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import WinOLS"),
                             tr("Impossible d'ouvrir %1").arg(f));
        return;
    }
    const QByteArray data = file.readAll();

    ecu::WinolsParser parser;
    auto result = parser.parse(data, QFileInfo(f).fileName());
    if (!result) {
        QMessageBox::warning(this, tr("Import WinOLS"),
                             tr("Échec de l'import : %1")
                                 .arg(QString::fromStdString(result.error().toStdString())));
        return;
    }

    m_doc->loadFromData(result->rom, result->filename);
    m_sidebar->showPanel(m_hexPanel);
    statusBar()->showMessage(
        tr("Importé : %1 (%2 Ko, %3 maps)")
            .arg(result->filename)
            .arg(result->rom.size() / 1024)
            .arg(result->maps.size()),
        5000);
}

// MainWindow::generateReport() est défini dans report_action.cpp — TU séparé
// pour éviter le conflit entre ecu::Characteristic (MapDiffer.hpp, tiré par
// ReportGenerator.hpp) et celui d'A2lParser.hpp inclus ici via a2l_panel.h.

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu(tr("Fichier"));
    fileMenu->addAction(tr("Nouveau projet"),    this, [this]() { m_projectPanel->newProject(); }, QKeySequence::New);
    fileMenu->addAction(tr("Ouvrir projet"),     this, [this]() { m_projectPanel->openProject(); }, QKeySequence::Open);
    fileMenu->addAction(tr("Ouvrir ROM..."),     this, [this]() {
        QString f = QFileDialog::getOpenFileName(this, tr("Ouvrir ROM"), {}, tr("ROM (*.bin *.ori *.mod);;Tous (*.*)"));
        if (!f.isEmpty()) m_hexPanel->loadRom(f);
    });
    fileMenu->addAction(tr("Importer projet WinOLS..."), this, &MainWindow::importWinols);
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
    toolsMenu->addSeparator();
    toolsMenu->addAction(tr("Générer rapport..."),  this,            &MainWindow::generateReport);

    auto* helpMenu = menuBar()->addMenu(tr("Aide"));
    helpMenu->addAction(tr("Vérifier les mises à jour"), this,
                        [this]() { checkForUpdates(/*silent=*/false); });
    helpMenu->addAction(tr("À propos d'ECU Studio"), this, [this]() {
        QMessageBox::about(this, tr("À propos d'ECU Studio"),
                           tr("ECU Studio v%1").arg(APP_VERSION));
    });
}

void MainWindow::checkForUpdates(bool silent) {
    if (silent) {
        // En mode silencieux, on interroge l'Updater directement et n'ouvre le
        // dialogue (qui relance sa propre vérification) que si une mise à jour
        // est effectivement disponible — sinon on ne dérange pas l'utilisateur.
        auto* conn = new QObject(this);
        auto cleanup = [conn]() { conn->deleteLater(); };
        connect(m_updater, &Updater::updateAvailable, conn,
                [this, conn](const QString&) {
                    conn->deleteLater();
                    auto* dlg = new UpdateDialog(m_updater, this);
                    dlg->setAttribute(Qt::WA_DeleteOnClose);
                    dlg->show();
                    dlg->startCheck();
                });
        connect(m_updater, &Updater::upToDate,   conn, cleanup);
        connect(m_updater, &Updater::checkError, conn, cleanup);
        m_updater->checkForUpdates();
        return;
    }

    auto* dlg = new UpdateDialog(m_updater, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    dlg->startCheck();
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
