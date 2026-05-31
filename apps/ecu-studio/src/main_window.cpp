#include "main_window.h"
#include "panels/mpps_panel.h"
#include "panels/hex_view_panel.h"
#include "panels/map_editor_panel.h"
#include "panels/map3d_panel.h"
#include "panels/project_panel.h"
#include "panels/automods_panel.h"
#include "panels/checksum_panel.h"
#include "panels/compare_panel.h"
#include "panels/git_panel.h"
#include "panels/a2l_panel.h"
#include "panels/can_panel.h"
#include "updater.h"
#include "update_dialog.h"
#include "rom_document.h"
#include "byte_span.h"
#include "ecu/WinolsParser.hpp"
#include "ecu/ReportGenerator.hpp"

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
#include <QDesktopServices>
#include <QUrl>

#include <span>
#include <cstdint>

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
    m_map3dPanel    = new Map3dPanel(m_doc, this);
    m_autoMods      = new AutoModsPanel(m_doc, this);
    m_checksumPanel = new ChecksumPanel(m_doc, this);
    m_comparePanel  = new ComparePanel(m_doc, this);
    m_gitPanel      = new GitPanel(m_doc, this);
    m_a2lPanel      = new A2lPanel(m_doc, this);
    m_canPanel      = new CanPanel(this);

    wirePanels();

    // Sidebar — réutilise SidebarNav de SocketSpy verbatim
    m_sidebar = new socketspy::gui::SidebarNav(this);
    m_sidebar->addPanel("\xf0\x9f\x93\x81",  tr("Projet"),    m_projectPanel);
    m_sidebar->addPanel("\xf0\x9f\x94\x8c",  tr("MPPS"),      m_mppsPanel);
    m_sidebar->addPanel("\xf0\x9f\x97\x83",  tr("Hex"),       m_hexPanel);
    m_sidebar->addPanel("\xf0\x9f\x93\x8a",  tr("Maps"),      m_mapEditor);
    // 🧊 (U+1F9CA, cube) — visualisation 3D de la map sélectionnée (à la WinOLS)
    m_sidebar->addPanel("\xf0\x9f\xa7\x8a",  tr("3D"),        m_map3dPanel);
    m_sidebar->addPanel("\xe2\x9a\x99",      tr("AutoMods"),  m_autoMods);
    m_sidebar->addPanel("\xe2\x9c\x85",      tr("Checksum"),  m_checksumPanel);
    m_sidebar->addPanel("\xe2\x89\xa0",      tr("Compare"),   m_comparePanel);
    m_sidebar->addPanel("\xe2\x93\x96",      tr("Git"),       m_gitPanel);
    m_sidebar->addPanel("A2L",               tr("A2L"),       m_a2lPanel);
    // 🚌 (U+1F68C, bus) — moniteur CAN intégré (cancore de SocketSpy)
    m_sidebar->addPanel("\xf0\x9f\x9a\x8c",  tr("CAN"),        m_canPanel);

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

    // « Voir en 3D » depuis Maps → pousse la map et bascule sur le panneau 3D.
    connect(m_mapEditor, &MapEditorPanel::view3dRequested, this,
            [this](quint32 address) {
                m_map3dPanel->showMap(address);
                m_sidebar->showPanel(m_map3dPanel);
            });
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

void MainWindow::generateReport() {
    if (!m_doc->isLoaded()) {
        QMessageBox::information(this, tr("Rapport"), tr("Aucune ROM chargée."));
        return;
    }

    // ROM originale optionnelle — sinon on compare la ROM à elle-même.
    const QString origPath = QFileDialog::getOpenFileName(
        this, tr("ROM originale pour comparaison (optionnel — Annuler pour ignorer)"),
        {}, tr("ROM (*.bin *.ori *.mod);;Tous (*.*)"));
    QByteArray original = m_doc->rom();
    if (!origPath.isEmpty()) {
        QFile of(origPath);
        if (of.open(QIODevice::ReadOnly)) original = of.readAll();
    }

    ecu::ReportInput in;
    in.project.name    = m_doc->name();
    in.project.ecu     = m_doc->ecuId();
    in.project.romName = m_doc->name();
    in.originalBuf = ecu_studio::constByteSpan(original);
    in.currentBuf  = ecu_studio::constByteSpan(m_doc->rom());

    auto html = ecu::ReportGenerator{}.generate(in);
    if (!html) {
        QMessageBox::warning(this, tr("Rapport"), tr("Génération impossible."));
        return;
    }

    const QString suggested =
        QString("rapport_%1.html").arg(m_doc->name().isEmpty() ? "ecu" : m_doc->name());
    QString out = QFileDialog::getSaveFileName(this, tr("Enregistrer le rapport"),
                                               suggested, tr("HTML (*.html)"));
    if (out.isEmpty()) return;

    QFile of(out);
    if (!of.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Rapport"), tr("Écriture impossible : %1").arg(out));
        return;
    }
    of.write(html->toUtf8());
    of.close();
    QDesktopServices::openUrl(QUrl::fromLocalFile(out));
}

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu(tr("Fichier"));
    fileMenu->addAction(tr("Nouveau projet"),    QKeySequence::New,  this, [this]() { m_projectPanel->newProject(); });
    fileMenu->addAction(tr("Ouvrir projet"),     QKeySequence::Open, this, [this]() { m_projectPanel->openProject(); });
    fileMenu->addAction(tr("Ouvrir ROM..."),     this, [this]() {
        QString f = QFileDialog::getOpenFileName(this, tr("Ouvrir ROM"), {}, tr("ROM (*.bin *.ori *.mod);;Tous (*.*)"));
        if (!f.isEmpty()) m_hexPanel->loadRom(f);
    });
    fileMenu->addAction(tr("Importer projet WinOLS..."), this, &MainWindow::importWinols);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Quitter"), QKeySequence::Quit, this, &QWidget::close);

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

    // Sous-menu de sélection de la langue (persistée via QSettings("language")).
    auto* langMenu = helpMenu->addMenu(tr("Langue / Language"));
    QSettings langSettings;
    const QString curLang = langSettings.value("language", "fr").toString();
    auto addLang = [&](const QString& label, const QString& code) {
        auto* act = langMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(curLang == code);
        connect(act, &QAction::triggered, this, [this, code]() {
            QSettings s;
            s.setValue("language", code);
            QMessageBox::information(this,
                tr("Langue modifiée"),
                tr("Veuillez redémarrer ECU Studio pour appliquer la nouvelle langue."));
        });
    };
    addLang("Français", "fr");
    addLang("English",  "en");
    helpMenu->addSeparator();

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
