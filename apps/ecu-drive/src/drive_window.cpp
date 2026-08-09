#include "drive_window.h"

#include "elm/Elm327.hpp"
#include "ecu/TunePackage.hpp"
#include "ecu/OpenDamos.hpp"
#include "ecu/Obd2.hpp"
#include "updater.h"
#include "platform.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLayout>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QCheckBox>
#include <QFrame>
#include <QStackedWidget>
#include <QScrollArea>
#include <QScroller>
#include <QSizePolicy>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#include <QSettings>
#include <QUrl>
#include <QFileInfo>
#include <QTimer>
#include <QProgressBar>
#include <QDir>
#include <QEventLoop>
#include <QSizePolicy>
#include <QEvent>
#include <QResizeEvent>
#include <QPixmap>
#include <QSize>
#include <QColor>
#include <thread>
#include <functional>
#include <type_traits>
#include <exception>
#include <algorithm>
#include <cmath>

#if defined(ELM_HAVE_BLUETOOTH)
#  include <QBluetoothDeviceDiscoveryAgent>
#  include <QBluetoothDeviceInfo>
#  include <QBluetoothLocalDevice>
#  include <QBluetoothAddress>
#endif

namespace ecu_drive {

namespace {
QString statusLabel(ecu::ValidationStatus s) {
    switch (s) {
        case ecu::ValidationStatus::Ok: return QStringLiteral("OK");
        case ecu::ValidationStatus::Warn: return QStringLiteral("Attention");
        case ecu::ValidationStatus::Fail: return QStringLiteral("Écart");
        default: return QStringLiteral("—");
    }
}
} // namespace

DriveWindow::DriveWindow(QWidget* parent) : QMainWindow(parent) {
    m_elm = new elm::Elm327(this);
    m_updater = new Updater(this);
    setWindowTitle(tr("ECU Drive %1").arg(QStringLiteral(APP_VERSION)));
    resize(420, 720);
    buildUi();

    connect(m_elm, &elm::Elm327::connected, this, [this](const QString& v) {
        m_connected = true;
        m_linkLossNotified = false;
        m_pendingDisconnectReason.clear();
        m_connectBtn->setEnabled(true);
        m_connectBtn->setText(tr("Déconnecter"));
        setStatus(tr("Connecté — %1").arg(v));
        platformToast(tr("ELM connecté"));
        refreshSessionButton();
        if (m_stack && m_stack->currentIndex() == 1)
            ensureSensorsPolling();
#if defined(ELM_HAVE_BLUETOOTH)
        if (m_btCombo && !m_btCombo->currentData().toString().isEmpty()
            && m_elm->isBluetoothTransport()) {
            QSettings().setValue(QStringLiteral("drive/lastBt"),
                                 m_btCombo->currentData().toString());
        }
#endif
        showInfoDialog(tr("Module connecté"),
            tr("ELM prêt : %1\n\nTu peux lancer une session conduite "
               "ou ouvrir Capteurs OBD.").arg(v));
    });
    connect(m_elm, &elm::Elm327::disconnected, this, [this]() {
        const bool intentional = m_userDisconnect;
        m_userDisconnect = false;
        m_connected = false;
        const bool hadSession = m_sessionOn;
        if (!intentional && hadSession && m_pendingDisconnectReason.isEmpty())
            m_pendingDisconnectReason = tr("Liaison Bluetooth perdue.");
        if (m_sessionOn) stopSession(); // affiche « Export des logs terminé » si ticks > 0
        m_connectBtn->setEnabled(true);
        m_connectBtn->setText(tr("Connecter"));
        refreshSessionButton();
        setStatus(tr("Déconnecté."));
        if (m_linkLossNotified) {
            // errorOccurred a déjà géré toast / export / dialogue.
            m_linkLossNotified = false;
            if (!hadSession && !m_pendingDisconnectReason.isEmpty())
                showInfoDialog(tr("Connexion"), m_pendingDisconnectReason);
            m_pendingDisconnectReason.clear();
            return;
        }
        // Une seule fenêtre : session → export ; sinon → connect/disconnect.
        if (hadSession)
            return;
        if (intentional) {
            showInfoDialog(tr("Déconnecté"),
                tr("Module déconnecté.\nTu peux reconnecter quand tu veux."));
        } else {
            showInfoDialog(tr("Module déconnecté"),
                tr("La liaison avec le dongle a été perdue.\n\n"
                   "Vérifie l'alimentation / la portée Bluetooth."));
        }
    });
    connect(m_elm, &elm::Elm327::errorOccurred, this, [this](const QString& e) {
        m_linkLossNotified = true;
        m_pendingDisconnectReason = e;
        m_connected = false;
        const bool hadSession = m_sessionOn;
        if (m_sessionOn) stopSession(); // fenêtre export logs
        m_connectBtn->setEnabled(true);
        m_connectBtn->setText(tr("Connecter"));
        refreshSessionButton();
        setStatus(e, true);
        platformToast(e.split(QLatin1Char('\n')).first());
        if (!hadSession)
            showInfoDialog(tr("Connexion"), e);
        // Si session : disconnected suivra et ne doublera pas (flag).
    });
    connect(m_elm, &elm::Elm327::status, this, [this](const QString& s) { setStatus(s); });
    connect(m_elm, &elm::Elm327::pidResult, this, &DriveWindow::onPid);
    connect(m_elm, &elm::Elm327::supportedPidsReady, this, [this](const QList<quint8>& pids) {
        m_ecuSupportedPids = QSet<quint8>(pids.begin(), pids.end());
        if (!m_stack || m_stack->currentIndex() != 1 || m_sessionOn || !m_connected)
            return;
        QList<std::uint8_t> qp;
        for (const auto& p : ecu::obd2::livePids()) {
            if (m_ecuSupportedPids.isEmpty() || m_ecuSupportedPids.contains(p.pid))
                qp.append(p.pid);
        }
        if (qp.isEmpty()) {
            for (const auto& p : ecu::obd2::livePids())
                qp.append(p.pid);
        }
        m_elm->startPolling(qp, 150);
        if (m_sensorsStatus) {
            m_sensorsStatus->setText(
                tr("Polling %1 PID(s) supportés (catalogue %2). N/S = non supporté.")
                    .arg(qp.size())
                    .arg(ecu::obd2::livePids().size()));
        }
        refreshSensorsTable();
    });

    connect(m_updater, &Updater::stateChanged, this, &DriveWindow::onUpdaterState);
    connect(m_updater, &Updater::progressChanged, this, &DriveWindow::onUpdaterState);
    connect(m_updater, &Updater::changelogChanged, this, [this]() {
        if (!m_updater->hasWhatsNew() || m_updater->updateAvailable())
            return;
        // Overlay in-app (QMessageBox modal souvent mort sous Qt Android).
        showInfoDialog(tr("Quoi de neuf (v%1)").arg(m_updater->currentVersion()),
                       m_updater->whatsNewNotes());
        m_updater->acknowledgeNotes();
    });

    // Différer ports / dernier tune : éviter de bloquer le premier show().
    QTimer::singleShot(0, this, [this]() {
        // Précharge la liste ECU en arrière-plan (évite le trou sans feedback
        // au premier import ROM sur Android).
        (void)availableEcuIds();
        // Intent Android « Ouvrir avec » prioritaire sur lastTune.
        consumeLaunchIntent();
        if (m_tunePath.isEmpty()) {
            QSettings s;
            const QString last = s.value(QStringLiteral("drive/lastTune")).toString();
            if (!last.isEmpty() && QFile::exists(last)) {
                m_suppressEcuPromptOnce = true;
                loadTuneFile(last);
            }
        }
        refreshPorts();
#if defined(ELM_HAVE_BLUETOOTH)
        selectLastBtDevice();
#endif
    });
    QTimer::singleShot(1500, m_updater, &Updater::check);
}

#if defined(ELM_HAVE_BLUETOOTH)
bool DriveWindow::likelyElmBtName(const QString& name) {
    const QString n = name.toUpper();
    return n.contains(QLatin1String("OBD"))
        || n.contains(QLatin1String("ELM"))
        || n.contains(QLatin1String("V-LINK"))
        || n.contains(QLatin1String("VLINK"))
        || n.contains(QLatin1String("VGATE"))
        || n.contains(QLatin1String("OBDII"));
}

void DriveWindow::ensureBtAgent() {
    if (m_btAgent) return;
    m_btAgent = new QBluetoothDeviceDiscoveryAgent(this);
    connect(m_btAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this,
            [this](const QBluetoothDeviceInfo& info) {
        if (!(info.coreConfigurations() & QBluetoothDeviceInfo::BaseRateCoreConfiguration))
            return;
        const QString addr = info.address().toString();
        for (const auto& d : m_btDevices)
            if (d.addr == addr) return;
        BtDevice d;
        d.addr = addr;
        d.name = info.name();
        d.likelyObd = likelyElmBtName(d.name);
        m_btDevices.push_back(std::move(d));
        rebuildBtCombo();
        refreshBtScanStatus();
        if (m_btCombo) m_btCombo->repaint();
    });
    connect(m_btAgent, &QBluetoothDeviceDiscoveryAgent::finished, this, [this]() {
        setBtScanning(false);
        selectBestBtDevice();
        const int shown = m_btCombo ? m_btCombo->count() : 0;
        const int found = static_cast<int>(m_btDevices.size());
        if (shown == 1) {
            setStatus(tr("Scan BT terminé — 1 appareil sélectionné :\n%1\n"
                         "Appuie sur Connecter (ou tape la liste pour changer).")
                          .arg(m_btCombo->currentText()));
            platformToast(tr("BT prêt : %1").arg(m_btCombo->currentText()));
        } else if (shown > 1) {
            setStatus(tr("Scan BT terminé — %1 affiché(s) / %2 trouvé(s).\n"
                         "Tape la liste Bluetooth pour choisir.")
                          .arg(shown).arg(found));
            platformToast(tr("Scan OK — choisis l'appareil"));
            // Ouvrir le sélecteur in-app (dropdown QComboBox cassé sur Android).
            QTimer::singleShot(100, this, [this]() { showBtDevicePicker(); });
        } else {
            setStatus(tr("Scan BT terminé — aucun appareil affiché (%1 trouvé(s)).\n"
                         "Décoche le filtre OBD / ELM, ou appairer le module.")
                          .arg(found), true);
            platformToast(tr("Scan BT : rien à sélectionner"));
        }
    });
    connect(m_btAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
            [this](QBluetoothDeviceDiscoveryAgent::Error) {
        setBtScanning(false);
        setStatus(tr("Échec scan Bluetooth (adaptateur / permissions)."), true);
        platformToast(tr("Échec scan Bluetooth"));
    });
    if (!m_btScanPulse) {
        m_btScanPulse = new QTimer(this);
        m_btScanPulse->setInterval(400);
        connect(m_btScanPulse, &QTimer::timeout, this, [this]() {
            if (m_btScanning)
                refreshBtScanStatus();
        });
    }
    if (!m_btScanWatchdog) {
        m_btScanWatchdog = new QTimer(this);
        m_btScanWatchdog->setSingleShot(true);
        connect(m_btScanWatchdog, &QTimer::timeout, this, [this]() {
            if (!m_btScanning) return;
            if (m_btAgent && m_btAgent->isActive())
                m_btAgent->stop();
            setBtScanning(false);
            selectBestBtDevice();
            const int found = static_cast<int>(m_btDevices.size());
            const int shown = m_btCombo ? m_btCombo->count() : 0;
            setStatus(tr("Scan BT arrêté — %1 affiché(s) / %2 trouvé(s).")
                          .arg(shown).arg(found));
            platformToast(tr("Scan BT terminé (%1)").arg(found));
            if (shown > 1)
                QTimer::singleShot(100, this, [this]() { showBtDevicePicker(); });
        });
    }
}

void DriveWindow::setBtScanning(bool on) {
    m_btScanning = on;
    if (m_scanBtBtn) {
        // Reste cliquable pendant le scan = Stop (sinon bouton mort jusqu'à finished).
        m_scanBtBtn->setEnabled(true);
        m_scanBtBtn->setText(on ? tr("Stop") : tr("Scan BT"));
    }
    if (on) {
        m_btScanStartedMs = QDateTime::currentMSecsSinceEpoch();
        if (m_btScanPulse && !m_btScanPulse->isActive())
            m_btScanPulse->start();
        if (m_btScanWatchdog)
            m_btScanWatchdog->start(20000);
    } else {
        if (m_btScanPulse) m_btScanPulse->stop();
        if (m_btScanWatchdog) m_btScanWatchdog->stop();
    }
}

void DriveWindow::refreshBtScanStatus() {
    if (!m_btScanning) return;
    const int sec = int((QDateTime::currentMSecsSinceEpoch() - m_btScanStartedMs) / 1000);
    const int found = static_cast<int>(m_btDevices.size());
    int obd = 0;
    for (const auto& d : m_btDevices)
        if (d.likelyObd) ++obd;
    setStatus(tr("Scan Bluetooth… %1 s — %2 trouvé(s) dont %3 OBD/ELM.\n"
                 "Laisse tourner, ou attends la fin automatique.")
                  .arg(sec)
                  .arg(found)
                  .arg(obd));
    if (m_statusLabel) {
        m_statusLabel->repaint();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
}

void DriveWindow::rebuildBtCombo() {
    if (!m_btCombo) return;
    const QString keep = m_btCombo->currentData().toString();
    m_btCombo->clear();
    bool onlyObd = m_btObdOnlyChk && m_btObdOnlyChk->isChecked();
    auto fill = [&](bool obdOnly) {
        m_btCombo->clear();
        std::vector<BtDevice> ordered = m_btDevices;
        std::stable_partition(ordered.begin(), ordered.end(),
                              [](const BtDevice& d) { return d.likelyObd; });
        for (const auto& d : ordered) {
            if (obdOnly && !d.likelyObd) continue;
            const QString label = (d.likelyObd ? QStringLiteral("★ ") : QString())
                + (d.name.isEmpty() ? d.addr
                                    : QStringLiteral("%1 (%2)").arg(d.name, d.addr));
            m_btCombo->addItem(label, d.addr);
        }
    };
    fill(onlyObd);
    // Filtre OBD trop strict (dongle sans nom) → afficher tout plutôt que liste vide.
    if (m_btCombo->count() == 0 && onlyObd && !m_btDevices.empty()) {
        if (m_btObdOnlyChk)
            m_btObdOnlyChk->setChecked(false);
        onlyObd = false;
        fill(false);
        setStatus(tr("Filtre OBD désactivé : %1 appareil(s) sans nom OBD/ELM.")
                      .arg(static_cast<int>(m_btDevices.size())));
        platformToast(tr("Filtre OBD off — vois tous les BT"));
    }
    if (m_btCombo->count() == 0) {
        m_btCombo->setPlaceholderText(tr("Aucun appareil — Scan BT"));
        return;
    }
    int idx = m_btCombo->findData(keep);
    if (idx < 0)
        idx = 0;
    // Préférer un OBD/ELM si présent.
    for (int i = 0; i < m_btCombo->count(); ++i) {
        if (m_btCombo->itemText(i).startsWith(QLatin1String("★"))) {
            idx = i;
            break;
        }
    }
    m_btCombo->setCurrentIndex(idx);
    m_btCombo->setEnabled(true);
}

void DriveWindow::selectBestBtDevice() {
    rebuildBtCombo();
    selectLastBtDevice();
    if (!m_btCombo || m_btCombo->count() == 0) return;
    // Si lastBt n'a rien sélectionné d'utile, forcer le meilleur.
    if (m_btCombo->currentData().toString().isEmpty())
        m_btCombo->setCurrentIndex(0);
    const QString addr = m_btCombo->currentData().toString();
    if (!addr.isEmpty())
        QSettings().setValue(QStringLiteral("drive/lastBt"), addr);
}

void DriveWindow::showBtDevicePicker() {
    if (!m_btPickerOverlay || !m_btPickerList) return;
    if (m_btDevices.empty()) {
        setStatus(tr("Aucun appareil — lance un Scan BT d'abord."), true);
        platformToast(tr("Lance Scan BT d'abord"));
        return;
    }
    rebuildBtCombo();
    m_btPickerList->clear();
    for (int i = 0; i < (m_btCombo ? m_btCombo->count() : 0); ++i) {
        auto* item = new QListWidgetItem(m_btCombo->itemText(i), m_btPickerList);
        item->setData(Qt::UserRole, m_btCombo->itemData(i));
        item->setSizeHint(QSize(0, 48));
    }
    if (m_btPickerList->count() == 0) {
        setStatus(tr("Liste vide — décoche le filtre OBD."), true);
        return;
    }
    const int cur = m_btCombo ? std::max(0, m_btCombo->currentIndex()) : 0;
    m_btPickerList->setCurrentRow(cur);
    layoutBusyOverlay();
    m_btPickerOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_btPickerOverlay->setVisible(true);
    m_btPickerOverlay->raise();
    m_btPickerOverlay->show();
    for (int i = 0; i < 3; ++i)
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    m_btPickerOverlay->repaint();
}

void DriveWindow::hideBtDevicePicker() {
    if (!m_btPickerOverlay) return;
    m_btPickerOverlay->setVisible(false);
    m_btPickerOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_btPickerOverlay->lower();
}

void DriveWindow::onBtPickerOk() {
    if (!m_btPickerList || !m_btCombo) return;
    QListWidgetItem* item = m_btPickerList->currentItem();
    hideBtDevicePicker();
    if (!item) return;
    const QString addr = item->data(Qt::UserRole).toString();
    const QString label = item->text();
    if (addr.isEmpty()) return;
    int idx = m_btCombo->findData(addr);
    if (idx < 0) {
        m_btCombo->addItem(label, addr);
        idx = m_btCombo->count() - 1;
    }
    m_btCombo->setCurrentIndex(idx);
    QSettings().setValue(QStringLiteral("drive/lastBt"), addr);
    setStatus(tr("BT sélectionné : %1").arg(label));
    platformToast(tr("BT : %1").arg(label));
}

void DriveWindow::onBtPickerCancel() {
    hideBtDevicePicker();
}

void DriveWindow::selectLastBtDevice() {
    if (!m_btCombo) return;
    const QString last = QSettings().value(QStringLiteral("drive/lastBt")).toString();
    if (last.isEmpty()) return;
    const int idx = m_btCombo->findData(last);
    if (idx >= 0) {
        m_btCombo->setCurrentIndex(idx);
        return;
    }
    // Pas encore dans la liste : proposer quand même
    m_btCombo->insertItem(0, tr("★ dernier (%1)").arg(last), last);
    m_btCombo->setCurrentIndex(0);
}
#endif

DriveWindow::~DriveWindow() {
    stopSession();
}

void DriveWindow::buildUi() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Bannière MAJ (style ColoCourse) — hors scroll pour rester visible
    m_updateBanner = new QFrame(central);
    m_updateBanner->setVisible(false);
    m_updateBanner->setStyleSheet(QStringLiteral(
        "QFrame { background:#1e3a5f; border:1px solid #3b82f6; border-radius:10px; margin:8px; }"));
    auto* ub = new QVBoxLayout(m_updateBanner);
    ub->setContentsMargins(10, 8, 10, 8);
    m_updateTitle = new QLabel(m_updateBanner);
    m_updateTitle->setStyleSheet(QStringLiteral("color:#e6edf3; font-weight:700;"));
    m_updateSub = new QLabel(m_updateBanner);
    m_updateSub->setWordWrap(true);
    m_updateSub->setStyleSheet(QStringLiteral("color:#93c5fd; font-size:12px;"));
    m_updateProgress = new QProgressBar(m_updateBanner);
    m_updateProgress->setRange(0, 100);
    m_updateProgress->setVisible(false);
    auto* ubRow = new QHBoxLayout;
    m_updateActionBtn = new QPushButton(tr("Mettre à jour"), m_updateBanner);
    m_updateActionBtn->setObjectName("accentBtn");
    m_updateDismissBtn = new QPushButton(tr("Plus tard"), m_updateBanner);
    connect(m_updateActionBtn, &QPushButton::clicked, this, &DriveWindow::onUpdateAction);
    connect(m_updateDismissBtn, &QPushButton::clicked, this, &DriveWindow::onUpdateDismiss);
    ubRow->addWidget(m_updateActionBtn, 1);
    ubRow->addWidget(m_updateDismissBtn);
    ub->addWidget(m_updateTitle);
    ub->addWidget(m_updateSub);
    ub->addWidget(m_updateProgress);
    ub->addLayout(ubRow);
    outer->addWidget(m_updateBanner);

    // Nav Drive / Capteurs
    auto* nav = new QHBoxLayout;
    nav->setContentsMargins(12, 4, 12, 4);
    auto* driveNav = new QPushButton(tr("Conduite"), central);
    auto* sensNav = new QPushButton(tr("Capteurs OBD"), central);
    driveNav->setObjectName("accentBtn");
    connect(driveNav, &QPushButton::clicked, this, &DriveWindow::showDrivePage);
    connect(sensNav, &QPushButton::clicked, this, &DriveWindow::showSensorsPage);
    nav->addWidget(driveNav, 1);
    nav->addWidget(sensNav, 1);
    outer->addLayout(nav);

    m_stack = new QStackedWidget(central);
    m_stack->addWidget(buildDrivePage(m_stack));
    m_stack->addWidget(buildSensorsPage(m_stack));
    outer->addWidget(m_stack, 1);

    m_statusLabel = new QLabel(tr("100 % local — aucune télémétrie.  v%1")
                                   .arg(QStringLiteral(APP_VERSION)), central);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setContentsMargins(12, 4, 12, 8);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#7c8fa6; font-size:12px;"));
    outer->addWidget(m_statusLabel);

    // Overlay plein écran — impossible à rater (contrairement à une bande en haut).
    m_busyOverlay = new QWidget(central);
    m_busyOverlay->setObjectName(QStringLiteral("busyOverlay"));
    m_busyOverlay->setStyleSheet(QStringLiteral(
        "#busyOverlay { background-color: #0f1520; }"));
    m_busyOverlay->setVisible(false);
    m_busyOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_busyOverlay->raise();
    auto* ovLay = new QVBoxLayout(m_busyOverlay);
    ovLay->setContentsMargins(24, 24, 24, 24);
    ovLay->addStretch();
    m_busyFrame = new QFrame(m_busyOverlay);
    m_busyFrame->setStyleSheet(QStringLiteral(
        "QFrame { background:#1e3a5f; border:2px solid #60a5fa; border-radius:14px; }"));
    auto* busyLay = new QVBoxLayout(m_busyFrame);
    busyLay->setContentsMargins(20, 18, 20, 18);
    busyLay->setSpacing(12);
    m_busyLabel = new QLabel(m_busyFrame);
    m_busyLabel->setWordWrap(true);
    m_busyLabel->setAlignment(Qt::AlignCenter);
    m_busyLabel->setStyleSheet(QStringLiteral(
        "color:#f8fafc; font-size:16px; font-weight:700; background:transparent; border:none;"));
    m_busyDetail = new QLabel(m_busyFrame);
    m_busyDetail->setWordWrap(true);
    m_busyDetail->setAlignment(Qt::AlignCenter);
    m_busyDetail->setStyleSheet(QStringLiteral(
        "color:#93c5fd; font-size:13px; background:transparent; border:none;"));
    m_busyBar = new QProgressBar(m_busyFrame);
    m_busyBar->setTextVisible(true);
    m_busyBar->setMinimumHeight(28);
    m_busyBar->setStyleSheet(QStringLiteral(
        "QProgressBar { background:#0f172a; border:1px solid #334155; border-radius:6px; text-align:center; color:#e2e8f0; }"
        "QProgressBar::chunk { background:#3b82f6; border-radius:5px; }"));
    busyLay->addWidget(m_busyLabel);
    busyLay->addWidget(m_busyDetail);
    busyLay->addWidget(m_busyBar);
    ovLay->addWidget(m_busyFrame);
    ovLay->addStretch();

    // Sélecteur ECU in-app : QInputDialog/QMessageBox modaux sont cassés sur
    // Qt Android (fenêtre invisible jusqu'à un switch d'app, OK sans effet).
    m_ecuPickerOverlay = new QWidget(central);
    m_ecuPickerOverlay->setObjectName(QStringLiteral("ecuPickerOverlay"));
    m_ecuPickerOverlay->setStyleSheet(QStringLiteral(
        "#ecuPickerOverlay { background-color: #0f1520; }"));
    m_ecuPickerOverlay->setVisible(false);
    m_ecuPickerOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto* pickLay = new QVBoxLayout(m_ecuPickerOverlay);
    pickLay->setContentsMargins(24, 24, 24, 24);
    pickLay->addStretch();
    auto* pickFrame = new QFrame(m_ecuPickerOverlay);
    pickFrame->setStyleSheet(QStringLiteral(
        "QFrame { background:#1e3a5f; border:2px solid #60a5fa; border-radius:14px; }"));
    auto* pf = new QVBoxLayout(pickFrame);
    pf->setContentsMargins(20, 18, 20, 18);
    pf->setSpacing(12);
    auto* pickTitle = new QLabel(tr("Choisir l'ECU"), pickFrame);
    pickTitle->setAlignment(Qt::AlignCenter);
    pickTitle->setStyleSheet(QStringLiteral(
        "color:#f8fafc; font-size:18px; font-weight:700; background:transparent; border:none;"));
    auto* pickHelp = new QLabel(
        tr("ROM brute (.bin) — sélectionne le type d'ECU\n"
           "(recette OpenDAMOS pour relocaliser les maps)."),
        pickFrame);
    pickHelp->setWordWrap(true);
    pickHelp->setAlignment(Qt::AlignCenter);
    pickHelp->setStyleSheet(QStringLiteral(
        "color:#93c5fd; font-size:13px; background:transparent; border:none;"));
    m_ecuPickerCombo = new QComboBox(pickFrame);
    m_ecuPickerCombo->setMinimumHeight(44);
    m_ecuPickerCombo->setMaxVisibleItems(10);
    auto* pickBtns = new QHBoxLayout;
    auto* cancelBtn = new QPushButton(tr("Annuler"), pickFrame);
    cancelBtn->setMinimumHeight(44);
    auto* okBtn = new QPushButton(tr("OK"), pickFrame);
    okBtn->setObjectName(QStringLiteral("accentBtn"));
    okBtn->setMinimumHeight(44);
    pickBtns->addWidget(cancelBtn);
    pickBtns->addWidget(okBtn, 1);
    pf->addWidget(pickTitle);
    pf->addWidget(pickHelp);
    pf->addWidget(m_ecuPickerCombo);
    pf->addLayout(pickBtns);
    pickLay->addWidget(pickFrame);
    pickLay->addStretch();
    connect(okBtn, &QPushButton::clicked, this, &DriveWindow::onEcuPickerOk);
    connect(cancelBtn, &QPushButton::clicked, this, &DriveWindow::onEcuPickerCancel);

#if defined(ELM_HAVE_BLUETOOTH)
    // Sélecteur BT in-app : le popup QComboBox ne s'ouvre pas dans un ScrollArea Android.
    m_btPickerOverlay = new QWidget(central);
    m_btPickerOverlay->setObjectName(QStringLiteral("btPickerOverlay"));
    m_btPickerOverlay->setStyleSheet(QStringLiteral(
        "#btPickerOverlay { background-color: #0f1520; }"));
    m_btPickerOverlay->setVisible(false);
    m_btPickerOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto* btLay = new QVBoxLayout(m_btPickerOverlay);
    btLay->setContentsMargins(24, 24, 24, 24);
    btLay->addStretch();
    auto* btFrame = new QFrame(m_btPickerOverlay);
    btFrame->setStyleSheet(QStringLiteral(
        "QFrame { background:#1e3a5f; border:2px solid #60a5fa; border-radius:14px; }"));
    auto* bf = new QVBoxLayout(btFrame);
    bf->setContentsMargins(20, 18, 20, 18);
    bf->setSpacing(12);
    auto* btTitle = new QLabel(tr("Choisir le Bluetooth"), btFrame);
    btTitle->setAlignment(Qt::AlignCenter);
    btTitle->setStyleSheet(QStringLiteral(
        "color:#f8fafc; font-size:18px; font-weight:700; background:transparent; border:none;"));
    auto* btHelp = new QLabel(
        tr("Sélectionne le dongle ELM327 (souvent OBDII / V-LINK),\n"
           "puis OK. Il doit être appairé (PIN 1234 ou 0000)."),
        btFrame);
    btHelp->setWordWrap(true);
    btHelp->setAlignment(Qt::AlignCenter);
    btHelp->setStyleSheet(QStringLiteral(
        "color:#93c5fd; font-size:13px; background:transparent; border:none;"));
    m_btPickerList = new QListWidget(btFrame);
    m_btPickerList->setMinimumHeight(160);
    m_btPickerList->setStyleSheet(QStringLiteral(
        "QListWidget { background:#111827; border:1px solid #334155; border-radius:8px; color:#e6edf3; }"
        "QListWidget::item { padding:10px; min-height:40px; }"
        "QListWidget::item:selected { background:#2563eb; color:#ffffff; }"));
    connect(m_btPickerList, &QListWidget::itemClicked, this, [this](QListWidgetItem*) {
        // Tap = sélection ; OK valide (évite double action accidentelle).
    });
    connect(m_btPickerList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        onBtPickerOk();
    });
    auto* btBtns = new QHBoxLayout;
    auto* btCancel = new QPushButton(tr("Annuler"), btFrame);
    btCancel->setMinimumHeight(44);
    auto* btOk = new QPushButton(tr("OK"), btFrame);
    btOk->setObjectName(QStringLiteral("accentBtn"));
    btOk->setMinimumHeight(44);
    btBtns->addWidget(btCancel);
    btBtns->addWidget(btOk, 1);
    bf->addWidget(btTitle);
    bf->addWidget(btHelp);
    bf->addWidget(m_btPickerList);
    bf->addLayout(btBtns);
    btLay->addWidget(btFrame);
    btLay->addStretch();
    connect(btOk, &QPushButton::clicked, this, &DriveWindow::onBtPickerOk);
    connect(btCancel, &QPushButton::clicked, this, &DriveWindow::onBtPickerCancel);
#endif

    // Dialogue info in-app (connexion module, fin d'export logs) — pas de QMessageBox.
    m_infoOverlay = new QWidget(central);
    m_infoOverlay->setObjectName(QStringLiteral("infoOverlay"));
    m_infoOverlay->setStyleSheet(QStringLiteral(
        "#infoOverlay { background-color: #0f1520; }"));
    m_infoOverlay->setVisible(false);
    m_infoOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto* infoLay = new QVBoxLayout(m_infoOverlay);
    infoLay->setContentsMargins(24, 24, 24, 24);
    infoLay->addStretch();
    auto* infoFrame = new QFrame(m_infoOverlay);
    infoFrame->setStyleSheet(QStringLiteral(
        "QFrame { background:#1e3a5f; border:2px solid #60a5fa; border-radius:14px; }"));
    auto* il = new QVBoxLayout(infoFrame);
    il->setContentsMargins(20, 18, 20, 18);
    il->setSpacing(14);
    m_infoTitle = new QLabel(infoFrame);
    m_infoTitle->setAlignment(Qt::AlignCenter);
    m_infoTitle->setWordWrap(true);
    m_infoTitle->setStyleSheet(QStringLiteral(
        "color:#f8fafc; font-size:18px; font-weight:700; background:transparent; border:none;"));
    m_infoBody = new QLabel(infoFrame);
    m_infoBody->setAlignment(Qt::AlignCenter);
    m_infoBody->setWordWrap(true);
    m_infoBody->setStyleSheet(QStringLiteral(
        "color:#93c5fd; font-size:14px; background:transparent; border:none;"));
    m_infoOkBtn = new QPushButton(tr("OK"), infoFrame);
    m_infoOkBtn->setObjectName(QStringLiteral("accentBtn"));
    m_infoOkBtn->setMinimumHeight(48);
    m_infoSecondaryBtn = new QPushButton(infoFrame);
    m_infoSecondaryBtn->setMinimumHeight(44);
    m_infoSecondaryBtn->setVisible(false);
    auto* infoBtns = new QVBoxLayout;
    infoBtns->setSpacing(8);
    infoBtns->addWidget(m_infoSecondaryBtn);
    infoBtns->addWidget(m_infoOkBtn);
    il->addWidget(m_infoTitle);
    il->addWidget(m_infoBody);
    il->addLayout(infoBtns);
    infoLay->addWidget(infoFrame);
    infoLay->addStretch();
    connect(m_infoOkBtn, &QPushButton::clicked, this, &DriveWindow::hideInfoDialog);
    connect(m_infoSecondaryBtn, &QPushButton::clicked, this, [this]() {
        auto fn = m_infoSecondaryAction;
        hideInfoDialog();
        if (fn) fn();
    });

    central->installEventFilter(this);
    layoutBusyOverlay();

    m_busyPulse = new QTimer(this);
    m_busyPulse->setInterval(250);
    connect(m_busyPulse, &QTimer::timeout, this, [this]() {
        if (!m_busyOverlay || !m_busyOverlay->isVisible()) return;
        const int sec = int((QDateTime::currentMSecsSinceEpoch() - m_busyStartedMs) / 1000);
        if (m_busyDetail) {
            m_busyDetail->setText(tr("Temps écoulé : %1 s — ne ferme pas l'app").arg(sec));
        }
        if (m_busyBar && m_busyBar->maximum() <= 0) {
            // Fait bouger la barre indéterminée / force le paint Android.
            m_busyBar->setValue(0);
            m_busyBar->update();
            m_busyOverlay->update();
        }
    });
}

namespace {

/** ScrollArea tactile (Android) : contenu en hauteur naturelle, pas de stretch. */
void setupScrollablePage(QScrollArea* scroll, QWidget* page) {
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Hauteur = taille du contenu (sinon le layout remplit le viewport → pas de scroll).
    page->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    page->setMinimumWidth(0);
#if defined(Q_OS_ANDROID)
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    scroll->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
    QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
    // Fallback doigt = souris sur certains firmwares Qt Android.
    QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);
#else
    QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);
#endif
    scroll->setWidget(page);
}

/** Force un rebuild d'affichage (bug Qt Android : pages Stack qui se chevauchent). */
void forceStackPageRefresh(QStackedWidget* stack, int index) {
    if (!stack || index < 0 || index >= stack->count()) return;
    QWidget* page = stack->widget(index);
    if (!page) return;
    stack->setCurrentIndex(index);

    // takeWidget/setWidget : seul moyen fiable de recharger le backing store Android.
    if (auto* scroll = qobject_cast<QScrollArea*>(page)) {
        if (QWidget* inner = scroll->takeWidget()) {
            scroll->setWidget(inner);
            inner->show();
            if (inner->layout())
                inner->layout()->activate();
            inner->updateGeometry();
            inner->adjustSize();
        }
        // Re-grab après takeWidget (sinon le scroll tactile meurt).
#if defined(Q_OS_ANDROID)
        scroll->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
        QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
        QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);
#else
        QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);
#endif
        scroll->viewport()->update();
        scroll->updateGeometry();
        scroll->update();
    }

    page->setVisible(false);
    page->setVisible(true);
    page->updateGeometry();
    page->repaint();
    stack->update();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

} // namespace

QWidget* DriveWindow::buildDrivePage(QWidget* parent) {
    auto* scroll = new QScrollArea(parent);

    auto* page = new QWidget;
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(12, 8, 12, 12);
    root->setSpacing(10);
    root->setSizeConstraint(QLayout::SetMinimumSize);

    auto* logoRow = new QHBoxLayout;
    auto* logo = new QLabel(page);
    QPixmap pm(QStringLiteral(":/ecu_studio_logo.png"));
    if (!pm.isNull())
        logo->setPixmap(pm.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoRow->addWidget(logo);
    auto* brand = new QLabel(tr("ECU Drive"), page);
    brand->setStyleSheet(QStringLiteral("color:#e6edf3; font-weight:700; font-size:18px;"));
    logoRow->addWidget(brand, 1);
    root->addLayout(logoRow);

    m_tuneLabel = new QLabel(tr("Aucun tune — importe un .ecutune ou une ROM .bin"), page);
    m_tuneLabel->setWordWrap(true);
    m_tuneLabel->setStyleSheet(QStringLiteral("color:#60a5fa; font-size:13px;"));
    root->addWidget(m_tuneLabel);

    auto* importBtn = new QPushButton(tr("Importer tune / ROM…"), page);
    importBtn->setMinimumHeight(44);
    importBtn->setObjectName("accentBtn");
    connect(importBtn, &QPushButton::clicked, this, &DriveWindow::importTune);
    root->addWidget(importBtn);

    // Connexion
#if defined(Q_OS_ANDROID)
    auto* usbHint = new QLabel(
        tr("USB OTG non supporté pour l'instant — utilise Bluetooth Classic."), page);
    usbHint->setWordWrap(true);
    usbHint->setStyleSheet(QStringLiteral("color:#f59e0b; font-size:12px;"));
    root->addWidget(usbHint);
    m_portCombo = new QComboBox(page);
    m_portCombo->setVisible(false);
#else
    auto* connRow = new QHBoxLayout;
    m_portCombo = new QComboBox(page);
    m_portCombo->setMinimumHeight(36);
    auto* refresh = new QPushButton(tr("↻"), page);
    connect(refresh, &QPushButton::clicked, this, &DriveWindow::refreshPorts);
    connRow->addWidget(m_portCombo, 1);
    connRow->addWidget(refresh);
    root->addLayout(connRow);
#endif

#if defined(ELM_HAVE_BLUETOOTH)
    auto* btRow = new QHBoxLayout;
    m_btCombo = new QComboBox(page);
    m_btCombo->setMinimumHeight(44);
    m_btCombo->setMaxVisibleItems(12);
    m_btCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_btCombo->setMinimumContentsLength(18);
    m_btCombo->setPlaceholderText(tr("Bluetooth ELM327…"));
    // Android : le popup natif du combo dans un ScrollArea ne s'ouvre pas.
    // On intercepte le clic pour ouvrir l'overlay in-app.
    m_btCombo->installEventFilter(this);
    m_scanBtBtn = new QPushButton(tr("Scan BT"), page);
    connect(m_scanBtBtn, &QPushButton::clicked, this, &DriveWindow::startBtScan);
    btRow->addWidget(m_btCombo, 1);
    btRow->addWidget(m_scanBtBtn);
    root->addLayout(btRow);

    m_btObdOnlyChk = new QCheckBox(tr("Filtrer OBD / ELM uniquement"), page);
    m_btObdOnlyChk->setChecked(
        QSettings().value(QStringLiteral("drive/btObdOnly"), true).toBool());
    connect(m_btObdOnlyChk, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("drive/btObdOnly"), on);
        rebuildBtCombo();
        selectLastBtDevice();
    });
    root->addWidget(m_btObdOnlyChk);
#else
    m_btCombo = nullptr;
    m_scanBtBtn = nullptr;
    m_btObdOnlyChk = nullptr;
    auto* noBt = new QLabel(tr("Bluetooth non compilé — USB seulement."), page);
    noBt->setStyleSheet(QStringLiteral("color:#f59e0b;"));
    root->addWidget(noBt);
#endif

    m_connectBtn = new QPushButton(tr("Connecter"), page);
    m_connectBtn->setMinimumHeight(44);
    connect(m_connectBtn, &QPushButton::clicked, this, &DriveWindow::toggleConnect);
    root->addWidget(m_connectBtn);

    m_beepChk = new QCheckBox(tr("Bip d'alerte underboost"), page);
    m_beepChk->setChecked(true);
    root->addWidget(m_beepChk);

    // Drive panel
    m_banner = new QFrame(page);
    m_banner->setMinimumHeight(80);
    m_banner->setStyleSheet(QStringLiteral(
        "QFrame { background:#1e293b; border-radius:10px; }"));
    auto* bl = new QVBoxLayout(m_banner);
    m_verdict = new QLabel(tr("Prêt"), m_banner);
    QFont vf = m_verdict->font();
    vf.setPointSizeF(22); vf.setBold(true);
    m_verdict->setFont(vf);
    m_verdict->setAlignment(Qt::AlignCenter);
    m_verdict->setStyleSheet(QStringLiteral("color:#e6edf3;"));
    bl->addWidget(m_verdict);
    root->addWidget(m_banner);

    m_boostBig = new QLabel(tr("— / — mbar"), page);
    QFont bf = m_boostBig->font();
    bf.setPointSizeF(26); bf.setBold(true);
    m_boostBig->setFont(bf);
    m_boostBig->setAlignment(Qt::AlignCenter);
    m_boostBig->setStyleSheet(QStringLiteral("color:#60a5fa;"));
    root->addWidget(m_boostBig);

    m_boostSub = new QLabel(tr("Δ —"), page);
    m_boostSub->setAlignment(Qt::AlignCenter);
    m_boostSub->setStyleSheet(QStringLiteral("color:#9ca3af; font-size:16px;"));
    root->addWidget(m_boostSub);

    auto* mapsTitle = new QLabel(tr("Écarts live (maps)"), page);
    mapsTitle->setStyleSheet(QStringLiteral("color:#94a3b8; font-size:12px; font-weight:600;"));
    root->addWidget(mapsTitle);
    m_mapsList = new QListWidget(page);
    m_mapsList->setMinimumHeight(120);
    m_mapsList->setMaximumHeight(180);
    m_mapsList->setStyleSheet(QStringLiteral(
        "QListWidget { background:#111827; border:1px solid #334155; border-radius:8px; color:#e6edf3; font-size:12px; }"
        "QListWidget::item { padding:6px 8px; }"));
    m_mapsList->addItem(tr("Lance une session pour voir les maps…"));
    root->addWidget(m_mapsList);

    m_rpmLoad = new QLabel(tr("RPM —  ·  Charge — %"), page);
    m_rpmLoad->setAlignment(Qt::AlignCenter);
    m_rpmLoad->setStyleSheet(QStringLiteral("color:#7c8fa6;"));
    root->addWidget(m_rpmLoad);

    m_sessionLive = new QLabel(tr("Session : —"), page);
    m_sessionLive->setAlignment(Qt::AlignCenter);
    m_sessionLive->setStyleSheet(QStringLiteral("color:#64748b; font-size:12px;"));
    root->addWidget(m_sessionLive);

    m_csvLabel = new QLabel(tr("CSV : inactif"), page);
    m_csvLabel->setAlignment(Qt::AlignCenter);
    m_csvLabel->setStyleSheet(QStringLiteral("color:#64748b; font-size:11px;"));
    root->addWidget(m_csvLabel);

    m_sessionBtn = new QPushButton(tr("▶  Lancer session conduite"), page);
    m_sessionBtn->setObjectName("accentBtn");
    m_sessionBtn->setMinimumHeight(56);
    QFont sf = m_sessionBtn->font();
    sf.setPointSizeF(sf.pointSizeF() + 3); sf.setBold(true);
    m_sessionBtn->setFont(sf);
    m_sessionBtn->setEnabled(false);
    connect(m_sessionBtn, &QPushButton::clicked, this, &DriveWindow::toggleSession);
    root->addWidget(m_sessionBtn);

    auto* shareBtn = new QPushButton(tr("Enregistrer le log sous…"), page);
    connect(shareBtn, &QPushButton::clicked, this, &DriveWindow::shareLastLog);
    root->addWidget(shareBtn);

#if defined(Q_OS_ANDROID)
    auto* shareSysBtn = new QPushButton(tr("Partager le log…"), page);
    connect(shareSysBtn, &QPushButton::clicked, this, &DriveWindow::shareLastLogSystem);
    root->addWidget(shareSysBtn);
#endif

    auto* updBtn = new QPushButton(tr("Vérifier les mises à jour"), page);
    connect(updBtn, &QPushButton::clicked, this, &DriveWindow::checkUpdatesManual);
    root->addWidget(updBtn);
    // Pas de addStretch() : sinon la page remplit le viewport et le scroll est mort.

    setupScrollablePage(scroll, page);
    return scroll;
}

QWidget* DriveWindow::buildSensorsPage(QWidget* parent) {
    auto* scroll = new QScrollArea(parent);

    auto* page = new QWidget;
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(12, 8, 12, 12);
    root->setSpacing(8);
    root->setSizeConstraint(QLayout::SetMinimumSize);

    auto* title = new QLabel(tr("Capteurs OBD mode 01"), page);
    title->setStyleSheet(QStringLiteral("color:#e6edf3; font-weight:700; font-size:16px;"));
    root->addWidget(title);

    m_sensorsStatus = new QLabel(
        tr("Connecte un ELM327 : découverte des PID supportés puis polling."), page);
    m_sensorsStatus->setWordWrap(true);
    m_sensorsStatus->setStyleSheet(QStringLiteral("color:#93c5fd; font-size:12px;"));
    root->addWidget(m_sensorsStatus);

    // Liste de labels (pas de QTableWidget : plantage fréquent sur Android
    // dans un QScrollArea / QStackedWidget).
    m_sensorValueLabels.clear();
    for (const auto& p : ecu::obd2::livePids()) {
        auto* row = new QWidget(page);
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(8, 6, 8, 6);
        row->setStyleSheet(QStringLiteral(
            "QWidget { background:#111827; border:1px solid #334155; border-radius:8px; }"));

        auto* name = new QLabel(QString::fromUtf8(p.name), row);
        name->setStyleSheet(QStringLiteral("color:#e6edf3; border:none; background:transparent;"));
        name->setWordWrap(true);

        auto* val = new QLabel(QStringLiteral("—"), row);
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        val->setStyleSheet(QStringLiteral(
            "color:#60a5fa; font-weight:700; font-size:16px; border:none; background:transparent;"));
        val->setMinimumWidth(72);

        auto* unit = new QLabel(QString::fromUtf8(p.unit), row);
        unit->setStyleSheet(QStringLiteral("color:#94a3b8; border:none; background:transparent;"));
        unit->setMinimumWidth(40);

        hl->addWidget(name, 1);
        hl->addWidget(val);
        hl->addWidget(unit);
        root->addWidget(row);
        m_sensorValueLabels.insert(p.pid, val);
    }

    auto* refreshBtn = new QPushButton(tr("Rafraîchir le polling"), page);
    refreshBtn->setMinimumHeight(44);
    connect(refreshBtn, &QPushButton::clicked, this, &DriveWindow::ensureSensorsPolling);
    root->addWidget(refreshBtn);

    setupScrollablePage(scroll, page);
    return scroll;
}

void DriveWindow::showDrivePage() {
    forceStackPageRefresh(m_stack, 0);
    if (m_sessionOn) return;
    if (m_connected && m_elm)
        m_elm->stopPolling();
    // Second tour après paint : corrige les résidus de la page Capteurs.
    QTimer::singleShot(50, this, [this]() {
        if (m_stack && m_stack->currentIndex() == 0)
            forceStackPageRefresh(m_stack, 0);
    });
}

void DriveWindow::showSensorsPage() {
    if (!m_stack) return;
    forceStackPageRefresh(m_stack, 1);
    // Différer le polling : laisser le stack peindre d'abord (évite crash Android).
    QTimer::singleShot(50, this, [this]() {
        if (!m_stack || m_stack->currentIndex() != 1) return;
        forceStackPageRefresh(m_stack, 1);
        ensureSensorsPolling();
        refreshSensorsTable();
    });
}

void DriveWindow::ensureSensorsPolling() {
    if (!m_connected || !m_elm) {
        if (m_sensorsStatus)
            m_sensorsStatus->setText(tr("Hors ligne — connecte un ELM327 d'abord."));
        return;
    }
    if (m_sessionOn) {
        if (m_sensorsStatus)
            m_sensorsStatus->setText(tr("Session conduite active — PID de validation."));
        return;
    }
    m_ecuSupportedPids.clear();
    if (m_sensorsStatus)
        m_sensorsStatus->setText(tr("Découverte des PID supportés par l'ECU…"));
    platformToast(tr("Scan PID OBD…"));
    m_elm->probeSupportedPids();
}

void DriveWindow::refreshSensorsTable() {
    for (auto it = m_sensorValueLabels.begin(); it != m_sensorValueLabels.end(); ++it) {
        QLabel* valItem = it.value();
        if (!valItem) continue;
        const quint8 pid = it.key();
        if (!m_live.contains(pid)) {
            if (!m_ecuSupportedPids.isEmpty() && !m_ecuSupportedPids.contains(pid)) {
                valItem->setText(tr("N/S"));
                valItem->setStyleSheet(QStringLiteral(
                    "color:#64748b; font-weight:600; font-size:14px; border:none; background:transparent;"));
            } else {
                valItem->setText(QStringLiteral("—"));
                valItem->setStyleSheet(QStringLiteral(
                    "color:#60a5fa; font-weight:700; font-size:16px; border:none; background:transparent;"));
            }
            continue;
        }
        const double v = m_live.value(pid);
        const QString unit = m_liveUnit.value(pid);
        QString text;
        if (pid == 0x0C || pid == 0x0D || pid == 0x1F || pid == 0x21 || pid == 0x31)
            text = QString::number(v, 'f', 0);
        else if (pid == 0x24 || pid == 0x42)
            text = QString::number(v, 'f', 3);
        else
            text = QString::number(v, 'f', 1);
        if (!unit.isEmpty())
            valItem->setToolTip(unit);
        valItem->setStyleSheet(QStringLiteral(
            "color:#60a5fa; font-weight:700; font-size:16px; border:none; background:transparent;"));
        valItem->setText(text);
    }
}

void DriveWindow::setStatus(const QString& msg, bool error) {
#if defined(ELM_HAVE_BLUETOOTH)
    // Ne pas écraser le feedback live du scan BT (ex. fin de reload ROM au boot).
    if (m_btScanning && !msg.contains(QStringLiteral("Scan")))
        return;
#endif
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color:#ef4444;")
                                       : QStringLiteral("color:#7c8fa6;"));
    m_statusLabel->setText(msg);
}

void DriveWindow::layoutBusyOverlay() {
    if (!centralWidget()) return;
    const QRect r = centralWidget()->rect();
    if (m_busyOverlay) {
        m_busyOverlay->setGeometry(r);
        if (m_busyOverlay->isVisible())
            m_busyOverlay->raise();
    }
    if (m_ecuPickerOverlay) {
        m_ecuPickerOverlay->setGeometry(r);
        if (m_ecuPickerOverlay->isVisible())
            m_ecuPickerOverlay->raise();
    }
#if defined(ELM_HAVE_BLUETOOTH)
    if (m_btPickerOverlay) {
        m_btPickerOverlay->setGeometry(r);
        if (m_btPickerOverlay->isVisible())
            m_btPickerOverlay->raise();
    }
#endif
    if (m_infoOverlay) {
        m_infoOverlay->setGeometry(r);
        if (m_infoOverlay->isVisible())
            m_infoOverlay->raise();
    }
}

bool DriveWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == centralWidget()
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        layoutBusyOverlay();
    }
#if defined(ELM_HAVE_BLUETOOTH)
    // Remplacer le popup QComboBox (souvent mort sous Android / ScrollArea).
    if (watched == m_btCombo
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonDblClick
            || event->type() == QEvent::MouseButtonRelease)) {
        if (event->type() != QEvent::MouseButtonRelease) {
            if (!m_btScanning)
                QTimer::singleShot(0, this, [this]() { showBtDevicePicker(); });
        }
        return true;
    }
#endif
    return QMainWindow::eventFilter(watched, event);
}

void DriveWindow::beginBusy(const QString& message, int max) {
    ++m_busyDepth;
    if (!m_busyOverlay) return;
    layoutBusyOverlay();
    m_busyOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_busyOverlay->setVisible(true);
    m_busyOverlay->raise();
    m_busyOverlay->show();
    m_busyStartedMs = QDateTime::currentMSecsSinceEpoch();
    if (m_busyLabel) m_busyLabel->setText(message);
    if (m_busyDetail)
        m_busyDetail->setText(tr("Temps écoulé : 0 s — ne ferme pas l'app"));
    if (m_busyBar) {
        if (max <= 0) {
            m_busyBar->setRange(0, 0); // indéterminé
            m_busyBar->setFormat(tr("En cours…"));
        } else {
            m_busyBar->setRange(0, max);
            m_busyBar->setValue(0);
            m_busyBar->setFormat(QStringLiteral("%v / %m"));
        }
    }
    setStatus(message);
    platformToast(message.split(QLatin1Char('\n')).first());
    if (m_busyPulse && !m_busyPulse->isActive())
        m_busyPulse->start();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // Plusieurs tours : Android peint souvent seulement après 2–3 processEvents.
    for (int i = 0; i < 3; ++i)
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    m_busyOverlay->repaint();
    if (m_busyLabel) m_busyLabel->repaint();
    if (m_busyBar) m_busyBar->repaint();
}

void DriveWindow::setBusy(int value, const QString& message) {
    if (m_busyDepth <= 0 || !m_busyOverlay || !m_busyOverlay->isVisible()) return;
    if (!message.isEmpty()) {
        if (m_busyLabel) m_busyLabel->setText(message);
        setStatus(message);
    }
    if (m_busyBar && m_busyBar->maximum() > 0 && value >= 0)
        m_busyBar->setValue(value);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void DriveWindow::endBusy() {
    if (m_busyDepth > 0)
        --m_busyDepth;
    if (m_busyDepth > 0) return;
    if (m_busyPulse) m_busyPulse->stop();
    if (m_busyOverlay) {
        // Sous Android un overlay hide() peut encore avaler les touches :
        // transparent + lower() après chargement tune (sinon Scan/Connect morts).
        m_busyOverlay->setVisible(false);
        m_busyOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_busyOverlay->lower();
        if (m_busyOverlay->parentWidget())
            m_busyOverlay->parentWidget()->update();
    }
    while (QApplication::overrideCursor())
        QApplication::restoreOverrideCursor();
    // Vider les progress callbacks encore en file (évite raise() fantôme).
    for (int i = 0; i < 3; ++i)
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    // Re-appliquer après les events queue'd qui auraient pu re-raise le busy.
    if (m_busyOverlay && m_busyDepth <= 0) {
        m_busyOverlay->setVisible(false);
        m_busyOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_busyOverlay->lower();
    }
    // Filet Android : certains firmwares réactivent l'overlay au paint suivant.
    QTimer::singleShot(50, this, [this]() {
        if (m_busyDepth > 0 || !m_busyOverlay) return;
        m_busyOverlay->setVisible(false);
        m_busyOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_busyOverlay->lower();
    });
}

void DriveWindow::refreshSessionButton() {
    if (!m_sessionBtn) return;
    if (m_sessionOn) {
        m_sessionBtn->setEnabled(true);
        return;
    }
    m_sessionBtn->setEnabled(m_connected && m_validator.isReady());
}

namespace {
/** Exécute un travail lourd hors UI tout en laissant animer la barre busy. */
template <typename Fn>
auto runWhileBusy(Fn&& fn) {
    using R = std::invoke_result_t<Fn>;
    QEventLoop loop;
    R result{};
    std::exception_ptr exc;
    std::thread worker([&]() {
        try {
            result = fn();
        } catch (...) {
            exc = std::current_exception();
        }
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();
    worker.join();
    if (exc)
        std::rethrow_exception(exc);
    return result;
}
} // namespace

void DriveWindow::importTune() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Importer tune / ROM"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("Tune / ROM (*.ecutune *.bin *.zip);;ROM brute (*.bin);;Package ECU Drive (*.ecutune *.zip);;Tous (*.*)"));
    if (path.isEmpty()) return;
    // Laisser le dialogue fichier / activity Android se fermer avant le busy.
    // Sur Android, un délai trop court → overlay ECU invisible jusqu'à un switch d'app.
#if defined(Q_OS_ANDROID)
    constexpr int kAfterPickerMs = 250;
#else
    constexpr int kAfterPickerMs = 0;
#endif
    QTimer::singleShot(kAfterPickerMs, this, [this, path]() { loadTuneFile(path); });
}

QStringList DriveWindow::availableEcuIds() {
    if (!m_ecuIdsCache.isEmpty())
        return m_ecuIdsCache;
    QStringList ids;
    auto addFrom = [&](const QString& root) {
        // entryList sur le préfixe qrc est bien plus rapide qu'un QDirIterator
        // récursif (surtout sur Android avec ~130 recettes embarquées).
        QDir dir(root);
        const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& ecu : subs) {
            if (QFile::exists(root + QLatin1Char('/') + ecu
                              + QStringLiteral("/open_damos.json"))
                && !ids.contains(ecu))
                ids << ecu;
        }
    };
    // Pas de busy ici : appelé au démarrage + à l'import ; l'appelant affiche
    // déjà l'overlay si besoin (évite double feedback / flash).
    addFrom(QStringLiteral(":/ressources"));
    addFrom(ecu::OpenDamos::userRecipeDir());
    addFrom(QStringLiteral("ressources"));
    ids.sort(Qt::CaseInsensitive);
    m_ecuIdsCache = ids;
    return m_ecuIdsCache;
}

void DriveWindow::showEcuPicker(const QByteArray& rom, const QString& path,
                                const QString& hint) {
    const QStringList ids = availableEcuIds();
    if (ids.isEmpty()) {
        platformToast(tr("Aucune recette OpenDAMOS embarquée."));
        showInfoDialog(tr("Import ROM"),
            tr("Aucune recette OpenDAMOS embarquée.\n"
               "Utilise un fichier .ecutune exporté depuis ECU Studio."));
        setStatus(tr("Import annulé — pas de recettes ECU."), true);
        return;
    }

    if (!m_autoEcuId.isEmpty() && ids.contains(m_autoEcuId)) {
        QSettings().setValue(QStringLiteral("drive/lastEcu"), m_autoEcuId);
        QTimer::singleShot(0, this, [this, rom, path, ecu = m_autoEcuId]() {
            applyRomBinary(rom, ecu, path);
        });
        return;
    }

    // Rechargement auto du dernier tune : garder le dernier ECU choisi.
    if (m_suppressEcuPromptOnce) {
        m_suppressEcuPromptOnce = false;
        QString ecu = hint;
        if (ecu.isEmpty())
            ecu = QSettings().value(QStringLiteral("drive/lastEcu")).toString();
        if (!ecu.isEmpty() && ids.contains(ecu)) {
            setStatus(tr("Rechargement ROM (%1)…").arg(ecu));
            platformToast(tr("Rechargement %1…").arg(ecu));
            QTimer::singleShot(0, this, [this, rom, path, ecu]() {
                applyRomBinary(rom, ecu, path);
            });
            return;
        }
    }

    m_pendingRom = rom;
    m_pendingRomPath = path;

    QString initial = hint;
    if (initial.isEmpty())
        initial = QSettings().value(QStringLiteral("drive/lastEcu"),
                                    QStringLiteral("edc16c34")).toString();
    if (!m_ecuPickerCombo || !m_ecuPickerOverlay) {
        // Fallback improbable : appliquer le premier ECU.
        QTimer::singleShot(0, this, [this, rom, path, ecu = ids.first()]() {
            applyRomBinary(rom, ecu, path);
        });
        return;
    }
    m_ecuPickerCombo->clear();
    m_ecuPickerCombo->addItems(ids);
    int idx = ids.indexOf(initial);
    if (idx < 0) idx = 0;
    m_ecuPickerCombo->setCurrentIndex(idx);

    layoutBusyOverlay();
    m_ecuPickerOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_ecuPickerOverlay->setVisible(true);
    m_ecuPickerOverlay->raise();
    m_ecuPickerOverlay->show();
    m_ecuPickerCombo->setFocus(Qt::OtherFocusReason);
    setStatus(tr("Choisis le type d'ECU pour « %1 »…")
                  .arg(QFileInfo(path).fileName()));
    platformToast(tr("Choisis l'ECU puis OK"));
    // Forcer le paint Android (sinon overlay invisible jusqu'à un switch d'app).
    for (int i = 0; i < 4; ++i)
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    m_ecuPickerOverlay->repaint();
    if (m_ecuPickerCombo) m_ecuPickerCombo->repaint();
}

void DriveWindow::hideEcuPicker() {
    if (!m_ecuPickerOverlay) return;
    m_ecuPickerOverlay->setVisible(false);
    m_ecuPickerOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_ecuPickerOverlay->lower();
}

void DriveWindow::onEcuPickerOk() {
    if (!m_ecuPickerCombo) return;
    const QString ecu = m_ecuPickerCombo->currentText().trimmed();
    if (ecu.isEmpty()) {
        setStatus(tr("Sélectionne un ECU."), true);
        platformToast(tr("Sélectionne un ECU"));
        return;
    }
    const QByteArray rom = m_pendingRom;
    const QString path = m_pendingRomPath;
    m_pendingRom.clear();
    m_pendingRomPath.clear();
    hideEcuPicker();
    QSettings().setValue(QStringLiteral("drive/lastEcu"), ecu);
    setStatus(tr("ECU %1 — démarrage de l'analyse…").arg(ecu));
    platformToast(tr("Analyse %1…").arg(ecu));
    // Différer : laisser l'overlay se fermer et peindre avant le travail lourd.
    QTimer::singleShot(0, this, [this, rom, path, ecu]() {
        applyRomBinary(rom, ecu, path);
    });
}

void DriveWindow::onEcuPickerCancel() {
    m_pendingRom.clear();
    m_pendingRomPath.clear();
    hideEcuPicker();
    setStatus(tr("Import annulé."));
    platformToast(tr("Import annulé"));
}

bool DriveWindow::loadRomBinaryFile(const QString& path) {
    const QFileInfo fi(path);
    const double mo = fi.size() > 0 ? fi.size() / (1024.0 * 1024.0) : 0.0;
    beginBusy(tr("Lecture de la ROM…\n%1 (%2 Mo)")
                  .arg(fi.fileName())
                  .arg(mo, 0, 'f', 1));

    // Lecture hors UI : sinon readAll() bloque le paint (écran figé = « planté »).
    struct ReadResult {
        bool ok = false;
        QByteArray data;
        QString error;
    };
    const ReadResult read = runWhileBusy([&]() {
        ReadResult r;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            r.error = tr("Impossible d'ouvrir %1").arg(path);
            return r;
        }
        r.data = f.readAll();
        r.ok = true;
        return r;
    });
    endBusy();

    if (!read.ok) {
        showInfoDialog(tr("Import"), read.error);
        return false;
    }
    const QByteArray& rom = read.data;
    if (rom.size() < 1024) {
        showInfoDialog(tr("Import"),
            tr("Fichier trop petit pour une ROM ECU (%1 octet(s)).").arg(rom.size()));
        return false;
    }
    if (rom.size() >= 4) {
        const quint32 sig = quint32(quint8(rom[0])) | (quint32(quint8(rom[1])) << 8)
                          | (quint32(quint8(rom[2])) << 16) | (quint32(quint8(rom[3])) << 24);
        if (sig == 0x04034B50u)
            return false;
    }

    // Après runWhileBusy (QEventLoop), un QInputDialog modal ne s'affiche pas
    // sur Android tant qu'on ne quitte pas l'app. Overlay in-app + singleShot.
#if defined(Q_OS_ANDROID)
    constexpr int kShowPickerMs = 100;
#else
    constexpr int kShowPickerMs = 0;
#endif
    QTimer::singleShot(kShowPickerMs, this, [this, rom = QByteArray(rom), path]() {
        showEcuPicker(rom, path);
    });
    return true;
}

void DriveWindow::loadTuneFile(const QString& path) {
    QString suffix = QFileInfo(path).suffix().toLower();
    // Android content:// : souvent sans extension → on lit la signature.
    if (suffix.isEmpty() || suffix.size() > 5) {
        QFile probe(path);
        if (probe.open(QIODevice::ReadOnly)) {
            const QByteArray head = probe.read(4);
            probe.close();
            if (head.size() >= 4) {
                const quint32 sig = quint32(quint8(head[0])) | (quint32(quint8(head[1])) << 8)
                                  | (quint32(quint8(head[2])) << 16) | (quint32(quint8(head[3])) << 24);
                if (sig == 0x04034B50u)
                    suffix = QStringLiteral("zip");
                else
                    suffix = QStringLiteral("bin"); // ROM brute typique
            } else {
                suffix = QStringLiteral("bin");
            }
        }
    }

    setStatus(tr("Import de %1…").arg(QFileInfo(path).fileName()));
    platformToast(tr("Import ROM en cours…"));

    if (suffix == QLatin1String("bin") || suffix == QLatin1String("rom")
        || suffix == QLatin1String("hex")) {
        if (loadRomBinaryFile(path))
            return;
        // ZIP mal nommé : tombe sur readZip ci-dessous.
    }
    if (suffix == QLatin1String("ecutune") || suffix == QLatin1String("zip")) {
        beginBusy(tr("Lecture du package .ecutune…"));
        auto pkg = runWhileBusy([&]() {
            return ecu::TunePackageIo::readZipFile(path);
        });
        endBusy();
        if (pkg) {
            applyTunePackage(*pkg, path);
            return;
        }
        showInfoDialog(tr("Import"), pkg.error());
        setStatus(tr("Import échoué."), true);
        return;
    }
    // Dernier recours : ROM brute
    if (!loadRomBinaryFile(path)) {
        showInfoDialog(tr("Import"),
            tr("Format non reconnu. Utilise un .ecutune (export ECU Studio) ou une ROM .bin."));
        setStatus(tr("Import échoué."), true);
    }
}

void DriveWindow::applyRomBinary(const QByteArray& rom, const QString& ecuId,
                                 const QString& path) {
    beginBusy(tr("Analyse ROM %1…\nRecherche des maps turbo / air / fuel.\n"
                 "Peut prendre 10–60 s — ne ferme pas l'app.")
                  .arg(ecuId));
    platformToast(tr("Analyse %1 en cours…").arg(ecuId));
    m_tuneLabel->setText(tr("Chargement ROM %1…").arg(ecuId));

    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    m_validator.setProgressCallback(
        [this, ecuId, t0](int cur, int total, const QString& mapName) {
            QMetaObject::invokeMethod(this, [this, ecuId, t0, cur, total, mapName]() {
                if (m_busyDepth <= 0 || !m_busyOverlay || !m_busyOverlay->isVisible())
                    return;
                if (!m_busyBar || !m_busyLabel) return;
                const int sec = int((QDateTime::currentMSecsSinceEpoch() - t0) / 1000);
                if (total > 0) {
                    if (m_busyBar->maximum() != total) {
                        m_busyBar->setRange(0, total);
                        m_busyBar->setFormat(QStringLiteral("%v / %m"));
                    }
                    m_busyBar->setValue(std::min(cur, total));
                }
                const QString map = mapName.isEmpty() ? tr("finalisation…") : mapName;
                const int shown = total > 0 ? std::min(cur + 1, total) : 1;
                const int tot   = total > 0 ? total : 1;
                m_busyLabel->setText(
                    tr("Map %1 / %2\n%3\nECU %4")
                        .arg(shown).arg(tot).arg(map, ecuId));
                if (m_busyDetail)
                    m_busyDetail->setText(
                        tr("Temps écoulé : %1 s — ne ferme pas l'app").arg(sec));
                setStatus(tr("Analyse %1/%2 (%3 s)").arg(shown).arg(tot).arg(sec));
                // update() suffit : raise() pendant/après analyse peut voler les touches.
                m_busyOverlay->update();
            }, Qt::QueuedConnection);
        });

    const bool ok = runWhileBusy([&]() {
        return m_validator.loadRom(rom, ecuId);
    });
    m_validator.clearProgressCallback();
    endBusy();
    while (m_busyDepth > 0) endBusy();
    // Laisser filtrer les progress encore queue'd avant de réactiver les touches.
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    if (!ok) {
        showInfoDialog(tr("Import ROM"),
            tr("Impossible de relocaliser les maps pour l'ECU « %1 ».\n"
               "Vérifie le type d'ECU, ou exporte un .ecutune depuis ECU Studio "
               "(qui embarque la recette OpenDAMOS).").arg(ecuId));
        setStatus(tr("Relocalisation échouée (%1).").arg(ecuId), true);
        m_tuneLabel->setText(tr("Échec import ROM — %1").arg(ecuId));
        refreshSessionButton();
        return;
    }
    m_tunePath = path;
    QSettings().setValue(QStringLiteral("drive/lastTune"), path);
    const int n = static_cast<int>(m_validator.rules().size());
    m_tuneLabel->setText(
        tr("ROM : %1\nECU %2 · MD5 %3 · %4 map(s) · prêt=%5")
            .arg(QFileInfo(path).fileName(),
                 ecuId,
                 m_validator.romMd5().left(8))
            .arg(n)
            .arg(m_validator.isReady() ? tr("oui") : tr("non")));
    refreshSessionButton();
    setStatus(m_validator.isReady()
                  ? tr("ROM chargée (%1 maps) — connecte l'ELM puis lance la session.")
                        .arg(n)
                  : tr("ROM chargée mais aucune map validable."),
              !m_validator.isReady());
    platformToast(tr("ROM OK — connecte le Bluetooth"));
}

void DriveWindow::applyTunePackage(const ecu::TunePackage& pkg, const QString& path) {
    beginBusy(tr("Application du tune (%1)…\nNe ferme pas l'app.").arg(pkg.manifest.ecuId));
    m_tuneLabel->setText(tr("Chargement tune %1…").arg(pkg.manifest.ecuId));
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    QTimer heart;
    heart.setInterval(400);
    QObject::connect(&heart, &QTimer::timeout, this, [this, t0]() {
        const int sec = int((QDateTime::currentMSecsSinceEpoch() - t0) / 1000);
        setStatus(tr("Application du tune… %1 s").arg(sec));
        if (m_busyBar) m_busyBar->update();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    });
    heart.start();
    m_validator.setProgressCallback(
        [this, t0](int cur, int total, const QString& mapName) {
            QMetaObject::invokeMethod(this, [this, t0, cur, total, mapName]() {
                if (m_busyDepth <= 0 || !m_busyOverlay || !m_busyOverlay->isVisible())
                    return;
                if (!m_busyBar || !m_busyLabel) return;
                const int sec = int((QDateTime::currentMSecsSinceEpoch() - t0) / 1000);
                if (total > 0) {
                    if (m_busyBar->maximum() != total) {
                        m_busyBar->setRange(0, total);
                        m_busyBar->setFormat(QStringLiteral("%v / %m"));
                    }
                    m_busyBar->setValue(std::min(cur, total));
                }
                m_busyLabel->setText(
                    tr("Map %1/%2 — %3 · %4 s")
                        .arg(total > 0 ? std::min(cur + 1, total) : 1)
                        .arg(total > 0 ? total : 1)
                        .arg(mapName.isEmpty() ? tr("…") : mapName)
                        .arg(sec));
            }, Qt::QueuedConnection);
        });
    runWhileBusy([&]() {
        if (!m_validator.loadTunePackage(pkg)) {
            // Peut échouer si fingerprints absents — on charge quand même les meta.
            m_validator.loadRomWithRecipe(pkg.rom, pkg.manifest.ecuId, pkg.recipeJson);
        }
        return true;
    });
    heart.stop();
    m_validator.clearProgressCallback();
    endBusy();
    while (m_busyDepth > 0) endBusy();
    m_tunePath = path;
    QSettings().setValue(QStringLiteral("drive/lastTune"), path);
    const int n = static_cast<int>(m_validator.rules().size());
    m_tuneLabel->setText(
        tr("Tune : %1\nECU %2 · MD5 %3 · %4 map(s) · prêt=%5")
            .arg(QFileInfo(path).fileName(),
                 pkg.manifest.ecuId,
                 pkg.manifest.romMd5.left(8))
            .arg(n)
            .arg(m_validator.isReady() ? tr("oui") : tr("non (relocalisation)")));
    refreshSessionButton();
    setStatus(m_validator.isReady()
                  ? tr("Tune prêt (%1 maps) — connecte l'ELM puis lance la session.")
                        .arg(n)
                  : tr("Tune chargé mais aucune map validable (ROM/recipe)."),
              !m_validator.isReady());
    platformToast(tr("Tune OK — connecte le Bluetooth"));
}

void DriveWindow::refreshPorts() {
    if (!m_portCombo) return;
#if defined(Q_OS_ANDROID)
    // USB OTG non câblé — ne pollue pas la combo cachée.
    m_portCombo->clear();
    m_portCombo->addItem(tr("(USB non supporté)"), QString());
    return;
#else
    const QString keep = m_portCombo->currentData().toString();
    m_portCombo->clear();
    m_portCombo->addItem(tr("(aucun port USB)"), QString());
    for (const auto& p : elm::Elm327::listPorts()) {
        const QString label = (p.likelyElm ? QStringLiteral("★ ") : QString())
                              + p.port + QStringLiteral(" — ") + p.description;
        m_portCombo->addItem(label, p.port);
    }
    const int idx = m_portCombo->findData(keep);
    if (idx >= 0) m_portCombo->setCurrentIndex(idx);
    else if (m_portCombo->count() > 1) m_portCombo->setCurrentIndex(1);
#endif
}

void DriveWindow::startBtScan() {
#if defined(ELM_HAVE_BLUETOOTH)
    if (!m_btCombo) return;
    if (m_btScanning) {
        startBtScanInternal(); // toggle stop
        return;
    }
    setStatus(tr("Autorisation Bluetooth…"));
    platformRequestBluetoothPermissions([this](bool granted) {
        if (!granted) {
            setStatus(tr("Permission Bluetooth refusée.\n"
                         "Active-la dans Réglages → Applications → ECU Drive."),
                      true);
            platformToast(tr("Permission BT refusée"));
            showInfoDialog(tr("Bluetooth"),
                tr("Sans permission Bluetooth, le scan et la connexion ELM "
                   "sont impossibles.\n\n"
                   "Réglages → Applications → ECU Drive → Autorisations."));
            return;
        }
        startBtScanInternal();
    });
#else
    setStatus(tr("Bluetooth non disponible dans ce build."), true);
#endif
}

#if defined(ELM_HAVE_BLUETOOTH)
void DriveWindow::startBtScanInternal() {
    if (!m_btCombo) return;
    ensureBtAgent();
    if (!m_btAgent) return;
    if (m_btScanning) {
        m_btAgent->stop();
        setBtScanning(false);
        rebuildBtCombo();
        setStatus(tr("Scan BT interrompu (%1 trouvé(s)).")
                      .arg(static_cast<int>(m_btDevices.size())));
        platformToast(tr("Scan interrompu"));
        return;
    }
    if (m_btAgent->isActive())
        m_btAgent->stop();
    m_btDevices.clear();
    m_btCombo->clear();
    m_btCombo->setPlaceholderText(tr("Recherche ELM327…"));
    setBtScanning(true);
    refreshBtScanStatus();
    platformToast(tr("Scan Bluetooth…"));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    m_btAgent->start(QBluetoothDeviceDiscoveryAgent::ClassicMethod);
}
#endif

void DriveWindow::toggleConnect() {
    if (m_connected) {
        setStatus(tr("Déconnexion…"));
        m_userDisconnect = true;
        m_connectBtn->setEnabled(false);
        m_elm->disconnectPort();
        m_connectBtn->setEnabled(true);
        return;
    }
    // Toujours un retour visible (sinon « rien ne se passe » après un import ROM).
    platformToast(tr("Connexion…"));
#if defined(ELM_HAVE_BLUETOOTH)
    if (m_btCombo && m_btCombo->currentIndex() >= 0
        && !m_btCombo->currentData().toString().isEmpty()) {
        platformRequestBluetoothPermissions([this](bool granted) {
            if (!granted) {
                setStatus(tr("Permission Bluetooth refusée."), true);
                platformToast(tr("Permission BT refusée"));
                return;
            }
            // Relance le flux connect une fois autorisé (évite duplication du corps).
            QTimer::singleShot(0, this, [this]() { toggleConnectAfterPerms(); });
        });
        return;
    }
#endif
    connectUsbOrFail();
}

#if defined(ELM_HAVE_BLUETOOTH)
void DriveWindow::toggleConnectAfterPerms() {
    if (m_connected) return;
    if (!m_btCombo || m_btCombo->currentData().toString().isEmpty()) {
        connectUsbOrFail();
        return;
    }
    const QString btLabel = m_btCombo->currentText();
    const QString addr = m_btCombo->currentData().toString();
#if defined(Q_OS_ANDROID) || defined(Q_OS_LINUX)
    QBluetoothLocalDevice local;
    if (local.isValid()) {
        const auto paired = local.pairingStatus(QBluetoothAddress(addr));
        if (paired == QBluetoothLocalDevice::Unpaired) {
            setStatus(tr("Module non appairé. Dans les réglages Bluetooth du "
                         "téléphone, appairer le dongle bleu (PIN 1234 ou 0000), "
                         "puis réessaie."), true);
            platformToast(tr("Appaire d'abord le module BT"));
            return;
        }
    }
#endif
    m_connectBtn->setEnabled(false);
    setStatus(tr("Connexion Bluetooth… (%1)").arg(btLabel));
    platformToast(tr("Connexion BT…"));
    m_elm->connectBluetooth(addr);
    QTimer::singleShot(8000, this, [this]() {
        if (!m_connected && m_connectBtn)
            m_connectBtn->setEnabled(true);
    });
}
#endif

void DriveWindow::connectUsbOrFail() {
    const QString port = m_portCombo ? m_portCombo->currentData().toString() : QString();
    if (port.isEmpty()) {
        setStatus(tr("Choisis un appareil Bluetooth (Scan BT), puis Connecter."), true);
        platformToast(tr("Choisis d'abord le Bluetooth"));
        return;
    }
    m_connectBtn->setEnabled(false);
    setStatus(tr("Connexion USB… (%1)").arg(port));
    m_elm->connectPort(port, 0);
    QTimer::singleShot(8000, this, [this]() {
        if (!m_connected && m_connectBtn)
            m_connectBtn->setEnabled(true);
    });
}

void DriveWindow::toggleSession() {
    if (m_sessionOn) stopSession();
    else startSession();
}

void DriveWindow::startSession() {
    if (m_sessionOn) return;
    if (!m_validator.isReady()) {
        setStatus(tr("Importe d'abord un tune / une ROM valide."), true);
        platformToast(tr("Importe un tune d'abord"));
        return;
    }
    if (!m_connected) {
        setStatus(tr("Connecte l'ELM (Bluetooth ou USB) avant de lancer la session."), true);
        platformToast(tr("Connecte le Bluetooth d'abord"));
        return;
    }
    beginBusy(tr("Démarrage de la session…"));
    m_hyst.reset();
    m_hyst.setFailThreshold(3);
    m_hyst.setOkThreshold(5);
    m_emaMeas.reset();
    m_emaExp.reset();
    m_lastAlertAt = 0;
    m_session.start(m_validator.ecuId(), m_validator.romMd5());
    const auto pids = m_validator.requiredPids();
    QList<std::uint8_t> qp;
    for (auto p : pids) qp.append(p);
    if (qp.isEmpty()) qp = { 0x0C, 0x04, 0x0B, 0x33 };
    m_elm->startPolling(qp, 180);
    m_sessionOn = true;
    m_sessionBtn->setText(tr("■  Arrêter session"));
    refreshSessionButton();
    autoStartCsv();
    m_verdict->setText(tr("Acquisition…"));
    endBusy();
    setStatus(tr("Session conduite active — %1 PID(s).").arg(qp.size()));
}

void DriveWindow::stopSession() {
    if (!m_sessionOn) return;
    beginBusy(tr("Arrêt de la session…"));
    m_elm->stopPolling();
    m_sessionOn = false;
    autoStopCsv();
    const auto sum = m_session.finish();
    m_sessionBtn->setText(tr("▶  Lancer session conduite"));
    refreshSessionButton();
    endBusy();
    if (sum.ticks > 0) showSummary(sum);
    else setStatus(tr("Session arrêtée (aucune donnée)."));
}

void DriveWindow::onPid(quint8 pid, double value, const QString&, const QString& unit) {
    m_live[pid] = value;
    if (!unit.isEmpty())
        m_liveUnit[pid] = unit;
    if (m_sessionOn) runValidation();
    if (m_stack && m_stack->currentIndex() == 1)
        refreshSensorsTable();
}

ecu::LivePidSnapshot DriveWindow::snapshot() const {
    ecu::LivePidSnapshot s;
    for (auto it = m_live.constBegin(); it != m_live.constEnd(); ++it)
        s[it.key()] = it.value();
    return s;
}

void DriveWindow::runValidation() {
    const auto results = m_validator.evaluateAll(snapshot());
    if (m_session.active()) m_session.ingest(results);
    updateDriveUi(results);
    refreshMapsList(results);
    appendCsv(results);
    const auto& c = m_session.current();
    m_sessionLive->setText(tr("OK %1 · Warn %2 · Fail %3 (%4 %)")
                               .arg(c.ok).arg(c.warn).arg(c.fail)
                               .arg(c.okRatio(), 0, 'f', 0));
}

std::optional<ecu::ValidationResult> DriveWindow::primaryBoost(
    const std::vector<ecu::ValidationResult>& results) const {
    for (const auto& r : results)
        if (r.category == QStringLiteral("boost")
            && r.status != ecu::ValidationStatus::NoData)
            return r;
    for (const auto& r : results)
        if (r.status != ecu::ValidationStatus::NoData) return r;
    return std::nullopt;
}

void DriveWindow::updateDriveUi(const std::vector<ecu::ValidationResult>& results) {
    const double rpm = m_live.value(0x0C, 0.0);
    const double load = m_live.value(0x04, 0.0);
    m_rpmLoad->setText(tr("RPM %1  ·  Charge %2 %")
                           .arg(rpm > 0 ? QString::number(rpm, 'f', 0) : QStringLiteral("—"))
                           .arg(load > 0 ? QString::number(load, 'f', 0) : QStringLiteral("—")));

    const auto boost = primaryBoost(results);
    if (!boost || boost->status == ecu::ValidationStatus::NoData) {
        m_verdict->setText(rpm > 400 ? tr("En attente…") : tr("Ralenti — accélère"));
        m_banner->setStyleSheet(QStringLiteral(
            "QFrame { background:#1e293b; border-radius:10px; }"));
        return;
    }
    const double meas = m_emaMeas.push(boost->measured);
    const double exp  = m_emaExp.push(boost->expected);
    const double d = meas - exp;
    const auto shown = m_hyst.update(boost->status);
    const QString unit = boost->unit.isEmpty() ? QStringLiteral("mbar") : boost->unit;
    m_boostBig->setText(tr("%1 / %2 %3").arg(meas, 0, 'f', 0).arg(exp, 0, 'f', 0).arg(unit));
    m_boostSub->setText(tr("Δ %1 %2").arg(d, 0, 'f', 0).arg(unit));

    QString bg;
    QString verdict;
    QColor col;
    switch (shown) {
        case ecu::ValidationStatus::Ok:
            bg = QStringLiteral("QFrame { background:#14532d; border-radius:10px; }");
            verdict = tr("TURBO OK"); col = QColor("#4ade80"); break;
        case ecu::ValidationStatus::Warn:
            bg = QStringLiteral("QFrame { background:#78350f; border-radius:10px; }");
            verdict = d < 0 ? tr("LÉGER UNDERBOOST") : tr("LÉGER OVERBOOST");
            col = QColor("#fbbf24"); break;
        case ecu::ValidationStatus::Fail:
            bg = QStringLiteral(
                "QFrame { background:#7f1d1d; border-radius:10px; border:3px solid #fca5a5; }");
            verdict = d < 0 ? tr("UNDERBOOST") : tr("OVERBOOST");
            col = QColor("#f87171");
            maybeAlert();
            break;
        default:
            bg = QStringLiteral("QFrame { background:#1e293b; border-radius:10px; }");
            verdict = tr("—"); col = QColor("#60a5fa"); break;
    }
    m_banner->setStyleSheet(bg);
    m_verdict->setText(verdict);
    m_boostBig->setStyleSheet(QStringLiteral("color:%1;").arg(col.name()));
}

void DriveWindow::refreshMapsList(const std::vector<ecu::ValidationResult>& results) {
    if (!m_mapsList) return;
    m_mapsList->clear();
    std::vector<ecu::ValidationResult> sorted = results;
    std::sort(sorted.begin(), sorted.end(),
              [](const ecu::ValidationResult& a, const ecu::ValidationResult& b) {
                  const double da = (a.status == ecu::ValidationStatus::NoData)
                                        ? -1.0 : std::abs(a.delta);
                  const double db = (b.status == ecu::ValidationStatus::NoData)
                                        ? -1.0 : std::abs(b.delta);
                  return da > db;
              });
    int shown = 0;
    for (const auto& r : sorted) {
        if (r.status == ecu::ValidationStatus::NoData) continue;
        const QString line = QStringLiteral("%1  %2  Δ%3 %4")
                                 .arg(statusLabel(r.status),
                                      r.mapName,
                                      QString::number(r.delta, 'f', 1),
                                      r.unit);
        auto* item = new QListWidgetItem(line, m_mapsList);
        if (r.status == ecu::ValidationStatus::Fail)
            item->setForeground(QColor(QStringLiteral("#f87171")));
        else if (r.status == ecu::ValidationStatus::Warn)
            item->setForeground(QColor(QStringLiteral("#fbbf24")));
        else
            item->setForeground(QColor(QStringLiteral("#4ade80")));
        if (++shown >= 8) break;
    }
    if (shown == 0)
        m_mapsList->addItem(tr("En attente de données map…"));
}

void DriveWindow::maybeAlert() {
    const int streak = m_hyst.failStreak();
    if (streak < 3 || streak == m_lastAlertAt || streak % 3 != 0) return;
    m_lastAlertAt = streak;
    if (m_beepChk->isChecked()) QApplication::beep();
}

void DriveWindow::autoStartCsv() {
    if (m_csv) return;
    // Scratch toujours writable (sandbox). L'utilisateur choisit ensuite
    // un emplacement accessible via « Enregistrer sous ».
    const QString dir = sessionLogScratchDir();
    if (!QDir().mkpath(dir)) {
        m_csvLabel->setText(tr("CSV : dossier inaccessible"));
        setStatus(tr("Impossible de créer le dossier logs :\n%1").arg(dir), true);
        platformToast(tr("Dossier logs inaccessible"));
        return;
    }
    m_lastCsv = dir + QStringLiteral("/drive_%1.csv")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    m_csv = new QFile(m_lastCsv, this);
    if (!m_csv->open(QIODevice::WriteOnly | QIODevice::Text)) {
        const QString err = m_csv->errorString();
        m_csv->deleteLater(); m_csv = nullptr;
        m_csvLabel->setText(tr("CSV : échec"));
        setStatus(tr("Écriture CSV impossible (%1) :\n%2").arg(err, m_lastCsv), true);
        platformToast(tr("Échec écriture CSV"));
        return;
    }
    m_session.setCsvPath(m_lastCsv);
    QTextStream(m_csv) << "time,map,measured,expected,delta,unit,status,rpm,load\n";
    m_csvLabel->setText(tr("CSV : %1").arg(QFileInfo(m_lastCsv).fileName()));
    setStatus(tr("Log CSV en cours — enregistrement sous à la fin"));
    platformToast(tr("Log CSV démarré"));
}

void DriveWindow::autoStopCsv() {
    if (!m_csv) return;
    m_csv->flush();
    m_csv->close();
    m_csv->deleteLater();
    m_csv = nullptr;
    m_csvLabel->setText(tr("CSV : %1 (terminé)").arg(QFileInfo(m_lastCsv).fileName()));
}

void DriveWindow::appendCsv(const std::vector<ecu::ValidationResult>& results) {
    if (!m_csv) return;
    QTextStream ts(m_csv);
    const QString t = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    for (const auto& r : results) {
        ts << t << ',' << r.mapName << ','
           << (r.status == ecu::ValidationStatus::NoData ? QString() : QString::number(r.measured, 'f', 2)) << ','
           << (r.status == ecu::ValidationStatus::NoData ? QString() : QString::number(r.expected, 'f', 2)) << ','
           << (r.status == ecu::ValidationStatus::NoData ? QString() : QString::number(r.delta, 'f', 2)) << ','
           << r.unit << ',' << statusLabel(r.status) << ','
           << r.xPhys << ',' << r.yPhys << '\n';
    }
    m_csv->flush();
}

QString DriveWindow::sessionLogScratchDir() const {
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!cache.isEmpty())
        return cache + QStringLiteral("/datalog");
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
         + QStringLiteral("/datalog");
}

QString DriveWindow::suggestedLogSaveDir() const {
    QSettings s;
    const QString custom = s.value(QStringLiteral("drive/logDir")).toString();
    if (!custom.isEmpty() && QDir(custom).exists())
        return custom;

    const QString dl = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!dl.isEmpty())
        return dl;

    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!docs.isEmpty())
        return docs;

    return sessionLogScratchDir();
}

QString DriveWindow::promptSaveLogAs(const QString& sourceCsv) {
    if (sourceCsv.isEmpty() || !QFile::exists(sourceCsv))
        return {};

    const QString name = QFileInfo(sourceCsv).fileName();
    const QString suggestDir = suggestedLogSaveDir();
    QDir().mkpath(suggestDir);
    const QString suggested = suggestDir + QLatin1Char('/') + name;

    const QString dest = QFileDialog::getSaveFileName(
        this, tr("Enregistrer le log sous"),
        suggested, tr("CSV (*.csv)"));
    if (dest.isEmpty())
        return {};

    const QString srcAbs = QFileInfo(sourceCsv).absoluteFilePath();
    const QString dstAbs = QFileInfo(dest).absoluteFilePath();
    if (!dstAbs.isEmpty() && srcAbs == dstAbs)
        return dest;

    // QFile::copy refuse d'écraser ; content:// Android : fallback read/write.
    if (QFile::exists(dest))
        QFile::remove(dest);

    bool ok = QFile::copy(sourceCsv, dest);
    if (!ok) {
        QFile in(sourceCsv);
        QFile out(dest);
        if (in.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const qint64 n = out.write(in.readAll());
            out.flush();
            ok = (n >= 0 && out.error() == QFile::NoError);
        }
    }
    if (!ok) {
        setStatus(tr("Échec Enregistrer sous :\n%1").arg(dest), true);
        platformToast(tr("Échec enregistrement log"));
        return {};
    }

    const QString parent = QFileInfo(dest).absolutePath();
    if (!parent.isEmpty() && !parent.startsWith(QStringLiteral("content:")))
        QSettings().setValue(QStringLiteral("drive/logDir"), parent);

    m_lastCsv = dest;
    m_session.setCsvPath(dest);
    m_csvLabel->setText(tr("CSV : %1").arg(QFileInfo(dest).fileName()));
    setStatus(tr("Log enregistré :\n%1").arg(dest));
    platformToast(tr("Log enregistré"));
    return dest;
}

void DriveWindow::showInfoDialog(const QString& title, const QString& body,
                                 const QString& okLabel) {
    showInfoDialog(title, body, okLabel, {}, {});
}

void DriveWindow::showInfoDialog(const QString& title, const QString& body,
                                 const QString& okLabel,
                                 const QString& secondaryLabel,
                                 std::function<void()> onSecondary) {
    if (!m_infoOverlay || !m_infoTitle || !m_infoBody || !m_infoOkBtn) {
        setStatus(title + QLatin1Char('\n') + body);
        platformToast(title);
        return;
    }
    m_infoTitle->setText(title);
    m_infoBody->setText(body);
    m_infoOkBtn->setText(okLabel.isEmpty() ? tr("OK") : okLabel);
    m_infoSecondaryAction = std::move(onSecondary);
    if (m_infoSecondaryBtn) {
        const bool hasSec = !secondaryLabel.isEmpty() && bool(m_infoSecondaryAction);
        m_infoSecondaryBtn->setVisible(hasSec);
        if (hasSec)
            m_infoSecondaryBtn->setText(secondaryLabel);
    }
    layoutBusyOverlay();
    m_infoOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_infoOverlay->setVisible(true);
    m_infoOverlay->raise();
    m_infoOverlay->show();
    for (int i = 0; i < 3; ++i)
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    m_infoOverlay->repaint();
    platformToast(title);
}

void DriveWindow::hideInfoDialog() {
    if (!m_infoOverlay) return;
    m_infoOverlay->setVisible(false);
    m_infoOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_infoOverlay->lower();
    m_infoSecondaryAction = {};
    if (m_infoSecondaryBtn)
        m_infoSecondaryBtn->setVisible(false);
}

void DriveWindow::showSummary(const ecu::SessionSummary& sum) {
    QString hot;
    for (const auto& h : sum.hotspots)
        hot += tr("\n  (%1,%2) ×%3 |Δ|%4")
                   .arg(h.gx).arg(h.gy).arg(h.count)
                   .arg(h.meanAbsDelta(), 0, 'f', 1);
    const QString csv = sum.csvPath.isEmpty() ? m_lastCsv : sum.csvPath;
    QString body = tr("Ticks %1\nOK %2 · Warn %3 · Fail %4\nDans tolérance : %5 %\n"
                      "Pic |Δ| %6 sur %7\nHotspots:%8\n\n"
                      "CSV prêt :\n%9")
                       .arg(sum.ticks).arg(sum.ok).arg(sum.warn).arg(sum.fail)
                       .arg(sum.okRatio(), 0, 'f', 1)
                       .arg(sum.peakAbsDelta, 0, 'f', 1)
                       .arg(sum.peakMap.isEmpty() ? QStringLiteral("—") : sum.peakMap)
                       .arg(hot.isEmpty() ? tr("\n  (aucun)") : hot)
                       .arg(csv.isEmpty() ? tr("(aucun)") : QFileInfo(csv).fileName());
    if (!m_pendingDisconnectReason.isEmpty()) {
        body += tr("\n\n—\nModule déconnecté pendant la session :\n%1")
                    .arg(m_pendingDisconnectReason);
        m_pendingDisconnectReason.clear();
    }
    body += tr("\n\nEnregistre ou partage le log pour le récupérer hors de l'app.");
    setStatus(tr("Session terminée — %1")
                  .arg(csv.isEmpty() ? QStringLiteral("—") : QFileInfo(csv).fileName()));
    platformToast(tr("Session terminée"));

    const QString scratch = csv;
    showInfoDialog(tr("Export des logs terminé"), body, tr("OK"),
                   tr("Enregistrer sous…"),
                   [this, scratch]() {
                       if (scratch.isEmpty() || !QFile::exists(scratch)) {
                           showInfoDialog(tr("Logs"), tr("Aucun fichier CSV à enregistrer."));
                           return;
                       }
                       const QString saved = promptSaveLogAs(scratch);
                       if (!saved.isEmpty()) {
                           showInfoDialog(tr("Log enregistré"),
                               tr("Fichier CSV enregistré ici :\n\n%1").arg(saved));
                       }
                   });
}

void DriveWindow::shareLastLog() {
    if (m_lastCsv.isEmpty() || !QFile::exists(m_lastCsv)) {
        showInfoDialog(tr("Logs"),
            tr("Aucun CSV de session récente.\n\n"
               "Lance une session conduite, puis utilise "
               "« Enregistrer le log sous… »."));
        return;
    }
    const QString saved = promptSaveLogAs(m_lastCsv);
    if (saved.isEmpty()) {
        setStatus(tr("Enregistrement annulé."));
        return;
    }
    showInfoDialog(tr("Log enregistré"),
        tr("Fichier CSV enregistré ici :\n\n%1").arg(saved));
}

void DriveWindow::shareLastLogSystem() {
    if (m_lastCsv.isEmpty() || !QFile::exists(m_lastCsv)) {
        showInfoDialog(tr("Logs"),
            tr("Aucun CSV de session récente à partager."));
        return;
    }
    if (!platformShareFile(m_lastCsv, QStringLiteral("text/csv"))) {
        // Fallback : Enregistrer sous puis l'utilisateur partage depuis Fichiers.
        showInfoDialog(tr("Partage"),
            tr("Partage système indisponible.\n"
               "Utilise « Enregistrer le log sous… » puis partage depuis Fichiers."),
            tr("OK"), tr("Enregistrer sous…"),
            [this]() { shareLastLog(); });
        return;
    }
    setStatus(tr("Partage système ouvert…"));
    platformToast(tr("Choisis une app pour partager"));
}

void DriveWindow::consumeLaunchIntent() {
    const QString uri = platformLaunchIntentUri(true);
    if (uri.isEmpty())
        return;
    setStatus(tr("Ouverture : %1").arg(uri));
    platformToast(tr("Import depuis Fichiers…"));
    loadTuneFile(uri);
}

void DriveWindow::onUpdaterState() { refreshUpdateBanner(); }

void DriveWindow::refreshUpdateBanner() {
    if (!m_updateBanner || !m_updater) return;
    using S = Updater::State;
    const S st = m_updater->state();
    const bool show = (st == S::Available || st == S::Downloading
                       || st == S::Ready || st == S::Failed);
    m_updateBanner->setVisible(show);
    if (!show) return;

    m_updateProgress->setVisible(st == S::Downloading);
    if (st == S::Downloading && m_updater->progressIndeterminate()) {
        m_updateProgress->setRange(0, 0); // animée (Android bufferise souvent le téléchargement)
    } else {
        m_updateProgress->setRange(0, 100);
        m_updateProgress->setValue(int(m_updater->progress() * 100.0 + 0.5));
    }
    m_updateActionBtn->setVisible(st != S::Downloading);
    m_updateDismissBtn->setVisible(st != S::Downloading);

    if (st == S::Downloading) {
        m_updateTitle->setText(tr("Téléchargement %1…").arg(m_updater->latestVersion()));
        m_updateSub->setText(tr("%1 — ne ferme pas l'app")
                                 .arg(m_updater->progressLabel()));
    } else if (st == S::Ready) {
        m_updateTitle->setText(tr("Version %1 prête").arg(m_updater->latestVersion()));
        m_updateSub->setText(tr("Appuie sur Installer pour lancer l'écran Android."));
        m_updateActionBtn->setText(tr("Installer"));
    } else if (st == S::Failed) {
        m_updateTitle->setText(tr("Échec de la mise à jour"));
        const QString err = m_updater->lastError();
        m_updateSub->setText(err.isEmpty()
                                 ? tr("Réessaie ou ouvre la page GitHub.")
                                 : err);
        m_updateActionBtn->setText(tr("Réessayer"));
    } else {
        m_updateTitle->setText(tr("Version %1 disponible").arg(m_updater->latestVersion()));
        const QString notes = m_updater->releaseNotes().trimmed();
        if (!notes.isEmpty()) {
            // Aperçu des notes dans la bannière (évite QMessageBox qui plante sur Android).
            QString preview = notes;
            preview.replace(QLatin1Char('\n'), QLatin1Char(' '));
            if (preview.size() > 160)
                preview = preview.left(157) + QStringLiteral("…");
            m_updateSub->setText(tr("Tu as la %1 — %2")
                                     .arg(m_updater->currentVersion(), preview));
        } else {
            m_updateSub->setText(tr("Tu as la %1").arg(m_updater->currentVersion()));
        }
        m_updateActionBtn->setText(tr("Mettre à jour"));
    }
}

void DriveWindow::onUpdateAction() {
    if (!m_updater) return;
    using S = Updater::State;
    const S st = m_updater->state();
    if (st == S::Ready) {
        setStatus(tr("Lancement de l'installateur Android…"));
        m_updater->install();
        return;
    }
    if (st == S::Failed) {
        setStatus(tr("Nouvelle tentative de mise à jour…"));
        // Si on a déjà un APK prêt, réessayer l'install ; sinon re-check.
        if (!m_updater->latestVersion().isEmpty() && m_updater->canInstall())
            m_updater->check();
        else
            m_updater->check();
        return;
    }
    if (st == S::Available) {
        // Pas de QMessageBox modal sur Android : il plantait / restait sans UI.
        // Les notes sont déjà dans la bannière ; on télécharge tout de suite.
        setStatus(tr("Téléchargement de la mise à jour %1…")
                      .arg(m_updater->latestVersion()));
        m_updater->download();
    }
}

void DriveWindow::onUpdateDismiss() {
    if (m_updater) m_updater->dismiss();
}

void DriveWindow::checkUpdatesManual() {
    if (!m_updater) return;
    setStatus(tr("Vérification des mises à jour…"));
    // Afficher le résultat même si déjà Idle / à jour.
    connect(m_updater, &Updater::stateChanged, this, [this]() {
        if (!m_updater) return;
        using S = Updater::State;
        const S st = m_updater->state();
        if (st == S::Checking || st == S::Downloading) return;
        if (st == S::Available || st == S::Ready)
            setStatus(tr("Mise à jour %1 disponible.").arg(m_updater->latestVersion()));
        else if (st == S::Failed)
            setStatus(m_updater->lastError().isEmpty()
                          ? tr("Échec de la vérification des mises à jour.")
                          : m_updater->lastError(),
                      true);
        else
            setStatus(tr("À jour (v%1).").arg(m_updater->currentVersion()));
    }, Qt::SingleShotConnection);
    m_updater->check();
}

} // namespace ecu_drive
