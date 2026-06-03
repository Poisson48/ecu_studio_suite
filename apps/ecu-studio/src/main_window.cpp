#include "main_window.h"
#include "panels/mpps_panel.h"
#include "panels/hex_view_panel.h"
#include "panels/map_editor_panel.h"
#include "panels/damos_editor_panel.h"
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
#include "welcome_screen.h"
#include "byte_span.h"
#include "ecu/WinolsParser.hpp"
#include "ecu/ReportGenerator.hpp"
#include "ecu/OpenDamos.hpp"

#include <QApplication>
#include <QMenuBar>
#include <QFile>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QFileInfo>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QUrl>

#include <algorithm>
#include <span>
#include <cstdint>

namespace ecu_studio {

namespace {
QString projectsRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/projects";
}

// Force le dialog Qt (pas natif GTK/portal) pour fiabiliser les filtres sur Linux.
QString pickRomFile(QWidget* parent, const QString& title, const QString& filter) {
    return QFileDialog::getOpenFileName(
        parent, title, {}, filter, nullptr, QFileDialog::DontUseNativeDialog);
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    m_updater = new Updater(this);
    m_doc     = new RomDocument(this);

    const QString root = projectsRoot();
    QDir().mkpath(root);
    m_projects = std::make_unique<ecu::ProjectManager>(root);

    // Charge la langue persistée avant de construire l'UI (tr() doit être prêt).
    {
        QSettings s;
        applyLanguage(s.value("language", "fr").toString());
    }

    setupUi();
    setupMenuBar();
    setupStatusBar();

    m_welcomeScreen = new WelcomeScreen(this);
    connectWelcomeSignals();

    restoreWindowState();
    updateRomStatus();

    // Autosave silencieux toutes les 60 s (si ROM modifiée et chemin connu).
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setInterval(60000);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::autoSave);
    m_autosaveTimer->start();

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
    m_damosEditor   = new DamosEditorPanel(m_doc, this);
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
    // 📝 (U+1F4DD, memo) — éditeur de recipe open_damos (modifier ou créer)
    m_sidebar->addPanel("\xf0\x9f\x93\x9d",  tr("DAMOS"),     m_damosEditor);
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

    // Persiste le panneau courant pour le restaurer au prochain lancement.
    connect(m_sidebar, &socketspy::gui::SidebarNav::currentPanelChanged, this,
            [this](QWidget*) {
                QSettings s;
                s.setValue("window/panelIndex", m_sidebar->stack()->currentIndex());
            });
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

    // « Voir en 3D » depuis Maps → pousse la map (avec unités) et bascule.
    connect(m_mapEditor, &MapEditorPanel::view3dRequested, this,
            [this](quint32 address, const QString& name,
                   const QString& xUnit, const QString& yUnit, const QString& dUnit) {
                m_map3dPanel->showMap(address, name, xUnit, yUnit, dUnit);
                m_sidebar->showPanel(m_map3dPanel);
            });

    // Quand l'éditeur DAMOS sauvegarde un recipe, MapEditor doit recharger
    // ses entrées (open_damos relit depuis le disque).
    connect(m_damosEditor, &DamosEditorPanel::recipeSaved, this,
            [this](const QString&, const QString&) {
                m_mapEditor->refreshFromCatalog();
            });

    // ECU inconnu : à la première chargée d'une ROM dont l'ECU n'a pas de recipe
    // open_damos, on propose à l'utilisateur d'en créer un et on l'envoie sur
    // l'éditeur DAMOS pré-rempli avec l'ECU id.
    connect(m_doc, &RomDocument::ecuChanged, this, [this](const QString& ecuId) {
        if (ecuId.isEmpty()) return;
        auto probe = ecu::OpenDamos::loadRecipe(ecuId);
        if (probe) return;  // recipe trouvé → rien à proposer
        const auto ans = QMessageBox::question(this,
            tr("ECU inconnu"),
            tr("Aucun recipe open_damos n'a été trouvé pour « %1 ».\n\n"
               "Créer un recipe vierge dans l'éditeur DAMOS ?").arg(ecuId),
            QMessageBox::Yes | QMessageBox::No);
        if (ans != QMessageBox::Yes) return;
        m_damosEditor->newEmptyRecipe(ecuId);
        m_sidebar->showPanel(m_damosEditor);
    });

    // Barre de statut live : se met à jour à chaque changement du document.
    connect(m_doc, &RomDocument::romLoaded,           this, &MainWindow::updateRomStatus);
    connect(m_doc, &RomDocument::ecuChanged,          this, [this](const QString&) { updateRomStatus(); });
    connect(m_doc, &RomDocument::modifiedStateChanged, this, [this](bool) { updateRomStatus(); });
    connect(m_doc, &RomDocument::romModified,         this,
            [this](qsizetype, qsizetype) { updateRomStatus(); });
}

void MainWindow::importWinols() {
    const QString f = pickRomFile(this, tr("Importer un export WinOLS"),
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
    const QString origPath = pickRomFile(
        this, tr("ROM originale pour comparaison (optionnel — Annuler pour ignorer)"),
        tr("ROM (*.bin *.ori *.mod);;Tous (*.*)"));
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

void MainWindow::openRomFromDialog() {
    const QString f = pickRomFile(this, tr("Ouvrir ROM"),
                                  tr("ROM (*.bin *.ori *.mod);;Tous (*.*)"));
    if (!f.isEmpty()) m_hexPanel->loadRom(f);
}

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu(tr("Fichier"));
    fileMenu->addAction(tr("Nouveau projet"),    QKeySequence::New,  this, [this]() { m_projectPanel->newProject(); });
    fileMenu->addAction(tr("Ouvrir projet"),     QKeySequence::Open, this, [this]() { m_projectPanel->openProject(); });
    fileMenu->addAction(tr("Ouvrir ROM..."), QKeySequence(Qt::CTRL | Qt::Key_R),
                        this, &MainWindow::openRomFromDialog);

    // Sous-menu « Projets récents » (reconstruit à l'ouverture du menu).
    m_recentMenu = fileMenu->addMenu(tr("Projets récents"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);

    fileMenu->addAction(tr("Importer projet WinOLS..."), this, &MainWindow::importWinols);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Enregistrer ROM..."), QKeySequence::Save, this, [this]() {
        if (!m_doc->isLoaded()) {
            QMessageBox::information(this, tr("Enregistrer"), tr("Aucune ROM chargée."));
            return;
        }
        const QString suggested = m_doc->name().isEmpty() ? QStringLiteral("rom.bin") : m_doc->name();
        const QString out = QFileDialog::getSaveFileName(
            this, tr("Enregistrer ROM"), suggested, tr("ROM (*.bin);;Tous (*.*)"));
        if (out.isEmpty()) return;
        if (m_doc->saveToFile(out))
            statusBar()->showMessage(tr("ROM enregistrée : %1").arg(out), 4000);
        else
            QMessageBox::warning(this, tr("Enregistrer"), tr("Écriture impossible : %1").arg(out));
    });
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

    // Sous-menu langue — miroir du sélecteur de l'écran d'accueil.
    auto* langMenu = helpMenu->addMenu(tr("Langue / Language"));
    QSettings langSettings;
    const QString curLang = langSettings.value("language", "fr").toString();
    auto* langGroup = new QActionGroup(this);
    langGroup->setExclusive(true);
    for (const auto& [label, code] : {
             std::pair<const char*, const char*>{"FR  Fran\xc3\xa7" "ais", "fr"},
             std::pair<const char*, const char*>{"EN  English",            "en"},
         }) {
        auto* act = langMenu->addAction(QString::fromUtf8(label));
        act->setCheckable(true);
        act->setChecked(curLang == QString::fromUtf8(code));
        langGroup->addAction(act);
        const QString codeStr = QString::fromUtf8(code);
        connect(act, &QAction::triggered, this, [this, codeStr]() {
            applyLanguage(codeStr);
        });
    }
    helpMenu->addSeparator();

    helpMenu->addAction(tr("Écran d'accueil..."), this, &MainWindow::showWelcome);
    helpMenu->addAction(tr("Vérifier les mises à jour"), this,
                        [this]() { checkForUpdates(/*silent=*/false); });
    helpMenu->addSeparator();
    helpMenu->addAction(tr("À propos d'ECU Studio"), this, &MainWindow::showAbout);
}

void MainWindow::rebuildRecentMenu() {
    if (!m_recentMenu) return;
    m_recentMenu->clear();

    auto listed = m_projects->list();
    QList<ecu::ProjectMeta> entries;
    if (listed) entries = *listed;
    std::sort(entries.begin(), entries.end(),
              [](const ecu::ProjectMeta& a, const ecu::ProjectMeta& b) {
                  return a.createdAt > b.createdAt;
              });

    if (entries.isEmpty()) {
        auto* act = m_recentMenu->addAction(tr("(aucun projet récent)"));
        act->setEnabled(false);
        return;
    }

    int count = 0;
    for (const ecu::ProjectMeta& e : entries) {
        if (count++ >= 10) break;
        const QString id = e.id;
        QString label = e.name;
        if (!e.ecu.isEmpty()) label += QStringLiteral("  (%1)").arg(e.ecu);
        m_recentMenu->addAction(label, this, [this, id]() { openProjectById(id); });
    }
}

void MainWindow::openProjectById(const QString& id) {
    auto meta = m_projects->get(id);
    if (!meta) {
        QMessageBox::warning(this, tr("Ouvrir projet"), tr("Projet introuvable."));
        return;
    }

    auto path = m_projects->romPath(id);
    if (path && QFile::exists(*path)) {
        if (!m_doc->loadFromFile(*path)) {
            QMessageBox::warning(this, tr("Ouvrir projet"),
                                 tr("Impossible de charger la ROM : %1").arg(*path));
            return;
        }
    } else {
        QMessageBox::information(this, tr("Ouvrir projet"),
            tr("Le projet « %1 » ne contient pas encore de ROM.").arg(meta->name));
    }

    m_doc->setEcuId(meta->ecu);
    m_autoMods->refresh();
    const QString romPath = m_doc->path();
    m_gitPanel->setRepoPath(romPath.isEmpty() ? QString()
                                              : QFileInfo(romPath).absolutePath());
    m_sidebar->showPanel(m_hexPanel);
    QSettings s;
    s.setValue("project/lastId", id);
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

void MainWindow::showAbout() {
    QMessageBox::about(this, tr("À propos d'ECU Studio"),
        QString("<b>ECU Studio v%1</b><br>"
                "%2<br><br>"
                "%3<br>"
                "<a href=\"https://github.com/Poisson48/ecu_studio_suite\" "
                "style=\"color:#6366f1;\">github.com/Poisson48/ecu_studio_suite</a>")
            .arg(APP_VERSION,
                 tr("Plateforme de reprogrammation ECU"),
                 tr("100% local · aucune télémétrie")));
}

void MainWindow::setupStatusBar() {
    // Message transitoire à gauche (showMessage), puis indicateurs permanents.
    m_romLabel = new QLabel(this);
    m_romLabel->setObjectName("romLabel");
    statusBar()->addPermanentWidget(m_romLabel);

    m_modifiedLabel = new QLabel(this);
    m_modifiedLabel->setObjectName("modifiedLabel");
    m_modifiedLabel->setToolTip(tr("ROM modifiée non enregistrée"));
    statusBar()->addPermanentWidget(m_modifiedLabel);

    m_ecuLabel = new QLabel(this);
    m_ecuLabel->setObjectName("ecuLabel");
    statusBar()->addPermanentWidget(m_ecuLabel);

    m_deviceLabel = new QLabel(tr("Aucun périphérique"), this);
    m_deviceLabel->setObjectName("deviceLabel");
    statusBar()->addPermanentWidget(m_deviceLabel);

    // Statut MPPS live.
    connect(m_mppsPanel, &MppsPanel::deviceStatusChanged, this,
            [this](const QString& status) { m_deviceLabel->setText(status); });
}

void MainWindow::updateRomStatus() {
    if (!m_romLabel) return;

    if (m_doc->isLoaded()) {
        const double kb = m_doc->rom().size() / 1024.0;
        m_romLabel->setText(tr("%1  ·  %2 Ko")
                                .arg(m_doc->name().isEmpty() ? tr("ROM") : m_doc->name())
                                .arg(kb, 0, 'f', 1));
    } else {
        m_romLabel->setText(tr("Aucune ROM"));
    }

    const QString ecu = m_doc->ecuId();
    m_ecuLabel->setText(ecu.isEmpty() ? QString() : tr("ECU : %1").arg(ecu));

    if (m_doc->isModified()) {
        m_modifiedLabel->setText(QStringLiteral("\xe2\x97\x8f"));
        m_modifiedLabel->setStyleSheet("color: #f59e0b; font-weight: 700;");
    } else {
        m_modifiedLabel->clear();
    }
}

// ── Écran d'accueil ───────────────────────────────────────────────────────────

void MainWindow::connectWelcomeSignals() {
    connect(m_welcomeScreen, &WelcomeScreen::newProjectRequested, this, [this]() {
        m_sidebar->showPanel(m_projectPanel);
        m_projectPanel->newProject();
    });
    connect(m_welcomeScreen, &WelcomeScreen::openRomRequested, this,
            &MainWindow::openRomFromDialog);
    connect(m_welcomeScreen, &WelcomeScreen::openProjectRequested, this, [this]() {
        m_sidebar->showPanel(m_projectPanel);
        m_projectPanel->openProject();
    });
    connect(m_welcomeScreen, &WelcomeScreen::openRecentProjectRequested, this,
            &MainWindow::openProjectById);
    connect(m_welcomeScreen, &WelcomeScreen::scanMppsRequested, this, [this]() {
        m_sidebar->showPanel(m_mppsPanel);
        m_mppsPanel->scanDevices();
    });
    connect(m_welcomeScreen, &WelcomeScreen::languageChanged, this,
            [this](const QString& code) {
                applyLanguage(code);
                recreateWelcomeScreen();
            });
}

void MainWindow::showWelcome() {
    m_welcomeScreen->refreshRecentProjects();
    m_welcomeScreen->show();
    m_welcomeScreen->raise();
    m_welcomeScreen->activateWindow();
}

// ── Persistance de l'état de la fenêtre ───────────────────────────────────────

void MainWindow::saveWindowState() {
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state", saveState());
    s.setValue("window/panelIndex", m_sidebar->stack()->currentIndex());
    if (!m_doc->path().isEmpty())
        s.setValue("rom/lastPath", m_doc->path());
}

void MainWindow::restoreWindowState() {
    QSettings s;
    const QByteArray geom = s.value("window/geometry").toByteArray();
    if (!geom.isEmpty()) restoreGeometry(geom);
    const QByteArray st = s.value("window/state").toByteArray();
    if (!st.isEmpty()) restoreState(st);

    const int idx = s.value("window/panelIndex", 0).toInt();
    if (idx >= 0 && idx < m_sidebar->stack()->count()) {
        if (auto* w = m_sidebar->stack()->widget(idx))
            m_sidebar->showPanel(w);
    }
}

void MainWindow::autoSave() {
    if (!m_doc->isLoaded() || !m_doc->isModified() || m_doc->path().isEmpty())
        return;
    if (m_doc->saveToFile(m_doc->path()))
        statusBar()->showMessage(tr("Sauvegarde automatique — %1").arg(m_doc->name()), 2000);
}

void MainWindow::applyLanguage(const QString& code) {
    QSettings s;
    s.setValue("language", code);

    if (m_translator) {
        qApp->removeTranslator(m_translator.get());
        m_translator.reset();
    }
    // Le français est la langue source : pas de .qm nécessaire.
    if (code != "fr") {
        m_translator = std::make_unique<QTranslator>();
        if (!m_translator->load(":/i18n/ecu_studio_" + code))
            m_translator.reset();
        else
            qApp->installTranslator(m_translator.get());
    }
}

void MainWindow::recreateWelcomeScreen() {
    if (m_welcomeScreen) {
        m_welcomeScreen->close();
        m_welcomeScreen->deleteLater();
    }
    m_welcomeScreen = new WelcomeScreen(this);
    connectWelcomeSignals();
    showWelcome();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    saveWindowState();
    QMainWindow::closeEvent(e);
}

} // namespace ecu_studio
