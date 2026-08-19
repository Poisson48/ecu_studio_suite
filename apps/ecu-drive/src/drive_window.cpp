#include "drive_window.h"

#include "elm/Elm327.hpp"
#include "ecu/TunePackage.hpp"
#include "ecu/OpenDamos.hpp"
#include "ecu/Obd2.hpp"
#include "ecu/SecurityAccess.hpp"
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
#include <QGuiApplication>
#include <QClipboard>
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
#include <QStyle>
#include <QPixmap>
#include <QSize>
#include <QColor>
#include <QLineEdit>
#include <QPlainTextEdit>
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
constexpr int kDtcStored  = 1;
constexpr int kDtcPending = 2;

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

    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState st) {
        m_uiSuspended = (st == Qt::ApplicationInactive
                         || st == Qt::ApplicationSuspended
                         || st == Qt::ApplicationHidden);
        // Ne jamais couper la session ici — le FGS garde le process vivant.
    });

    connect(m_elm, &elm::Elm327::connected, this, [this](const QString& v) {
        m_connected = true;
        m_linkLossNotified = false;
        m_pendingDisconnectReason.clear();
        m_connectBtn->setEnabled(true);
        m_connectBtn->setText(tr("Déconnecter"));
        setStatus(tr("Connecté — %1").arg(v));
        platformToast(tr("ELM connecté — prêt"));
        refreshSessionButton();
        refreshDiagButtons();
        if (m_stack && m_stack->currentIndex() == 1)
            ensureSensorsPolling();
        else if (m_stack && m_stack->currentIndex() == 2)
            ensureTurboPolling();
#if defined(ELM_HAVE_BLUETOOTH)
        if (m_btCombo && !m_btCombo->currentData().toString().isEmpty()
            && m_elm->isBluetoothTransport()) {
            QSettings().setValue(QStringLiteral("drive/lastBt"),
                                 m_btCombo->currentData().toString());
        }
#endif
        // Pas d'overlay bloquant : le toast + statut suffisent pour enchaîner.
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
        m_dtcAwaiting = 0;
        m_dtcClearPending = false;
        refreshSessionButton();
        refreshDiagButtons();
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
        m_dtcAwaiting = 0;
        m_dtcClearPending = false;
        refreshSessionButton();
        refreshDiagButtons();
        setStatus(e, true);
        platformToast(e.split(QLatin1Char('\n')).first());
        if (!hadSession)
            showInfoDialog(tr("Connexion"), e);
        // Si session : disconnected suivra et ne doublera pas (flag).
    });
    connect(m_elm, &elm::Elm327::status, this, [this](const QString& s) {
        setStatus(s);
        if (!m_dtcClearPending) return;
        m_dtcClearPending = false;
        const bool ok = s.contains(QLatin1String("effacé"), Qt::CaseInsensitive);
        if (ok) {
            m_dtcFlags.clear();
            refreshDtcList();
        }
        if (m_dtcStatus) {
            m_dtcStatus->setText(ok ? tr("Codes effacés — relis pour confirmer.")
                                    : s);
        }
        refreshDiagButtons();
        resumePollingAfterDtc();
    });
    connect(m_elm, &elm::Elm327::pidResult, this, &DriveWindow::onPid);
    connect(m_elm, &elm::Elm327::rawResponse, this, &DriveWindow::onRawResponse);
    connect(m_elm, &elm::Elm327::securityAccessResult, this,
            [this](bool success, const QString& keyHex, const QString& detail) {
        if (m_rawLog) {
            if (success)
                m_rawLog->appendPlainText(tr("[SA] Déverrouillé — key : %1").arg(keyHex));
            else
                m_rawLog->appendPlainText(tr("[SA] Échec — %1").arg(detail));
        }
        if (m_saResultLabel) {
            m_saResultLabel->setStyleSheet(success
                ? QStringLiteral("color:#34d399; font-weight:700; font-family:monospace; font-size:18px;")
                : QStringLiteral("color:#f87171; font-weight:700; font-family:monospace; font-size:15px;"));
            m_saResultLabel->setText(success ? keyHex : tr("Échec : %1").arg(detail));
        }
    });
    connect(m_elm, &elm::Elm327::dtcsReady, this,
            [this](const QStringList& codes, bool pending) {
        mergeDtcCodes(codes, pending);
        if (m_dtcAwaiting > 0) --m_dtcAwaiting;
        if (m_dtcAwaiting == 0) {
            refreshDtcList();
            const int n = m_dtcFlags.size();
            if (m_dtcStatus) {
                m_dtcStatus->setText(n == 0
                                         ? tr("Aucun code défaut (modes 03 + 07).")
                                         : tr("%1 code(s) défaut (modes 03 + 07).").arg(n));
            }
            setStatus(n == 0 ? tr("Aucun DTC")
                             : tr("%1 DTC").arg(n));
            platformToast(n == 0 ? tr("Aucun DTC")
                                 : tr("%1 DTC").arg(n));
            refreshDiagButtons();
            resumePollingAfterDtc();
        }
    });
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
    // Queued : laisse le paint Android tourner entre les ticks de progression.
    connect(m_updater, &Updater::progressChanged, this, &DriveWindow::onUpdaterState,
            Qt::QueuedConnection);
    connect(m_updater, &Updater::changelogChanged, this, [this]() {
        if (!m_updater->hasWhatsNew() || m_updater->updateAvailable())
            return;
        // Overlay in-app (QMessageBox modal souvent mort sous Qt Android).
        showInfoDialog(tr("Quoi de neuf (v%1)").arg(m_updater->currentVersion()),
                       m_updater->whatsNewNotes());
        m_updater->acknowledgeNotes();
    });

    // Différer ports / dernier tune : d'abord les autorisations runtime Android.
    QTimer::singleShot(250, this, [this]() {
        platformRequestStartupPermissions([this](bool allOk) {
            if (!allOk) {
                showInfoDialog(
                    tr("Autorisations nécessaires"),
                    tr("ECU Drive a besoin de :\n\n"
                       "• Appareils à proximité — scan / connexion ELM Bluetooth\n"
                       "• Fichiers / stockage — enregistrer les logs CSV "
                       "(Android 10 et moins)\n"
                       "• Position — scan Bluetooth sur Android 11 et moins\n\n"
                       "Sans elles, le Bluetooth ou l'enregistrement peuvent échouer."),
                    tr("Continuer"),
                    tr("Ouvrir les réglages"),
                    []() { platformOpenAppSettings(); });
            }
            (void)availableEcuIds();
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

    // Nav Drive / Capteurs / Diagnostic — un seul onglet « accent » à la fois.
    auto* nav = new QHBoxLayout;
    nav->setContentsMargins(12, 8, 12, 4);
    nav->setSpacing(6);
    m_driveNavBtn = new QPushButton(tr("Conduite"), central);
    m_sensNavBtn = new QPushButton(tr("Capteurs"), central);
    m_diagNavBtn = new QPushButton(tr("Diagnostic"), central);
    for (QPushButton* b : {m_driveNavBtn, m_sensNavBtn, m_diagNavBtn}) {
        b->setMinimumHeight(44);
        b->setCheckable(true);
        b->setAutoExclusive(true);
    }
    connect(m_driveNavBtn, &QPushButton::clicked, this, &DriveWindow::showDrivePage);
    connect(m_sensNavBtn, &QPushButton::clicked, this, &DriveWindow::showSensorsPage);
    connect(m_diagNavBtn, &QPushButton::clicked, this, &DriveWindow::showDiagPage);
    nav->addWidget(m_driveNavBtn, 1);
    nav->addWidget(m_sensNavBtn, 1);
    nav->addWidget(m_diagNavBtn, 1);
    outer->addLayout(nav);

    m_stack = new QStackedWidget(central);
    m_stack->addWidget(buildDrivePage(m_stack));
    m_stack->addWidget(buildSensorsPage(m_stack));
    m_stack->addWidget(buildDiagPage(m_stack));
    outer->addWidget(m_stack, 1);
    setNavPage(0);
    refreshDiagButtons();

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

/** Force un redraw propre de la page active (sans takeWidget qui corrompt le rendu). */
void forceStackPageRefresh(QStackedWidget* stack, int index) {
    if (!stack || index < 0 || index >= stack->count()) return;
    QWidget* page = stack->widget(index);
    if (!page) return;
    stack->setCurrentIndex(index);

    if (auto* scroll = qobject_cast<QScrollArea*>(page)) {
        if (QWidget* inner = scroll->widget()) {
            if (inner->layout())
                inner->layout()->activate();
            inner->updateGeometry();
        }
        scroll->viewport()->update();
        scroll->updateGeometry();
        scroll->update();
    }

    page->updateGeometry();
    page->update();
    stack->update();
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

    // CTA principal juste sous Connecter (pas sous le fold).
    m_sessionBtn = new QPushButton(tr("▶  Lancer session conduite"), page);
    m_sessionBtn->setObjectName("accentBtn");
    m_sessionBtn->setMinimumHeight(56);
    QFont sf = m_sessionBtn->font();
    sf.setPointSizeF(sf.pointSizeF() + 3); sf.setBold(true);
    m_sessionBtn->setFont(sf);
    m_sessionBtn->setEnabled(false);
    connect(m_sessionBtn, &QPushButton::clicked, this, &DriveWindow::toggleSession);
    root->addWidget(m_sessionBtn);

    m_beepChk = new QCheckBox(tr("Bip d'alerte underboost"), page);
    m_beepChk->setChecked(true);
    root->addWidget(m_beepChk);

    // Drive panel
    m_banner = new QFrame(page);
    m_banner->setMinimumHeight(96);
    m_banner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_banner->setStyleSheet(QStringLiteral(
        "QFrame { background:#1e293b; border-radius:10px; }"));
    auto* bl = new QVBoxLayout(m_banner);
    bl->setContentsMargins(10, 12, 10, 12);
    m_verdict = new QLabel(tr("Prêt"), m_banner);
    QFont vf = m_verdict->font();
    vf.setPointSizeF(20); vf.setBold(true);
    m_verdict->setFont(vf);
    m_verdict->setAlignment(Qt::AlignCenter);
    m_verdict->setWordWrap(true);
    m_verdict->setMinimumHeight(36);
    m_verdict->setStyleSheet(QStringLiteral("color:#e6edf3; background:transparent;"));
    bl->addWidget(m_verdict);
    root->addWidget(m_banner);

    m_boostBig = new QLabel(tr("— / — mbar"), page);
    QFont bf = m_boostBig->font();
    bf.setPointSizeF(24); bf.setBold(true);
    m_boostBig->setFont(bf);
    m_boostBig->setAlignment(Qt::AlignCenter);
    m_boostBig->setWordWrap(true);
    m_boostBig->setMinimumHeight(40);
    m_boostBig->setStyleSheet(QStringLiteral("color:#60a5fa;"));
    root->addWidget(m_boostBig);

    m_boostSub = new QLabel(tr("Δ —"), page);
    m_boostSub->setAlignment(Qt::AlignCenter);
    m_boostSub->setWordWrap(true);
    m_boostSub->setStyleSheet(QStringLiteral("color:#9ca3af; font-size:16px;"));
    root->addWidget(m_boostSub);

    auto* mapsTitle = new QLabel(tr("Écarts live (maps)"), page);
    mapsTitle->setStyleSheet(QStringLiteral("color:#94a3b8; font-size:12px; font-weight:600;"));
    root->addWidget(mapsTitle);

    // Pas de QListWidget dans le ScrollArea (gestes + crop Android).
    m_mapsListHost = new QWidget(page);
    m_mapsListLay = new QVBoxLayout(m_mapsListHost);
    m_mapsListLay->setContentsMargins(0, 0, 0, 0);
    m_mapsListLay->setSpacing(4);
    auto* mapsPlaceholder = new QLabel(tr("Lance une session pour voir les maps…"), m_mapsListHost);
    mapsPlaceholder->setWordWrap(true);
    mapsPlaceholder->setStyleSheet(QStringLiteral(
        "QLabel { background:#111827; border:1px solid #334155; border-radius:8px; "
        "color:#94a3b8; font-size:12px; padding:8px; }"));
    m_mapsListLay->addWidget(mapsPlaceholder);
    root->addWidget(m_mapsListHost);

    m_rpmLoad = new QLabel(tr("RPM —  ·  Charge — %"), page);
    m_rpmLoad->setAlignment(Qt::AlignCenter);
    m_rpmLoad->setWordWrap(true);
    m_rpmLoad->setStyleSheet(QStringLiteral("color:#7c8fa6;"));
    root->addWidget(m_rpmLoad);

    m_sessionLive = new QLabel(tr("Session : —"), page);
    m_sessionLive->setAlignment(Qt::AlignCenter);
    m_sessionLive->setWordWrap(true);
    m_sessionLive->setStyleSheet(QStringLiteral("color:#64748b; font-size:12px;"));
    root->addWidget(m_sessionLive);

    m_csvLabel = new QLabel(tr("CSV : inactif"), page);
    m_csvLabel->setAlignment(Qt::AlignCenter);
    m_csvLabel->setWordWrap(true);
    m_csvLabel->setStyleSheet(QStringLiteral("color:#64748b; font-size:11px;"));
    root->addWidget(m_csvLabel);

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

QWidget* DriveWindow::buildDiagPage(QWidget* parent) {
    auto* scroll = new QScrollArea(parent);

    auto* page = new QWidget;
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(12, 8, 12, 12);
    root->setSpacing(10);
    root->setSizeConstraint(QLayout::SetMinimumSize);

    // ── Turbo / wastegate (live + protocole) ────────────────────────────
    auto* turboTitle = new QLabel(tr("Turbo / wastegate"), page);
    turboTitle->setStyleSheet(QStringLiteral("color:#e6edf3; font-weight:700; font-size:16px;"));
    root->addWidget(turboTitle);

    m_turboStatus = new QLabel(
        tr("Connecte un ELM327 : MAP, baro, Δboost, MAF et régime en live."), page);
    m_turboStatus->setWordWrap(true);
    m_turboStatus->setStyleSheet(QStringLiteral("color:#93c5fd; font-size:12px;"));
    root->addWidget(m_turboStatus);

    auto addTurboRow = [&](const QString& name, QLabel*& valueOut) {
        auto* row = new QWidget(page);
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(8, 6, 8, 6);
        row->setStyleSheet(QStringLiteral(
            "QWidget { background:#111827; border:1px solid #334155; border-radius:8px; }"));
        auto* n = new QLabel(name, row);
        n->setStyleSheet(QStringLiteral("color:#e6edf3; border:none; background:transparent;"));
        n->setWordWrap(true);
        valueOut = new QLabel(QStringLiteral("—"), row);
        valueOut->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueOut->setStyleSheet(QStringLiteral(
            "color:#60a5fa; font-weight:700; font-size:16px; border:none; background:transparent;"));
        valueOut->setMinimumWidth(100);
        hl->addWidget(n, 1);
        hl->addWidget(valueOut);
        root->addWidget(row);
    };
    addTurboRow(tr("MAP (collecteur)"), m_turboMap);
    addTurboRow(tr("Baro"), m_turboBaro);
    addTurboRow(tr("Δ boost (MAP − baro)"), m_turboDelta);
    addTurboRow(tr("MAF"), m_turboMaf);
    addTurboRow(tr("Régime"), m_turboRpm);

    auto* protoTitle = new QLabel(tr("Test turbo — procédure générale"), page);
    protoTitle->setStyleSheet(QStringLiteral("color:#e6edf3; font-weight:700; font-size:14px;"));
    root->addWidget(protoTitle);

    auto* proto = new QLabel(
        tr("1. Moteur chaud, véhicule à l’arrêt : MAP ≈ baro (Δ ≈ 0), MAF bas.\n"
           "2. Accélération franche (3e ou 4e, 2000–4000 tr/min) : MAP doit monter "
           "nettement au-dessus de la pression baro — la valeur cible dépend de l’ECU "
           "et du tune actif.\n"
           "3. Si MAP reste proche de baro quelle que soit la charge, le turbo ne pousse "
           "pas : vérifier vanne wastegate, géométrie variable, ou durites.\n"
           "4. Relever Δboost max et les DTC actifs (mode 03/07) pour orienter le "
           "diagnostic.\n\n"
           "Actionneur constructeur : les routines de pilotage actif (wastegate, VGT) "
           "nécessitent une session étendue déverrouillée — voir section "
           "« Accès constructeur » ci-dessous ou utilise la console brute."),
        page);
    proto->setWordWrap(true);
    proto->setStyleSheet(QStringLiteral("color:#cbd5e1; font-size:12px;"));
    root->addWidget(proto);

    auto* rawTitle = new QLabel(tr("Console OBD / KWP brute"), page);
    rawTitle->setStyleSheet(QStringLiteral("color:#e6edf3; font-weight:700; font-size:14px;"));
    root->addWidget(rawTitle);

    auto* rawHint = new QLabel(
        tr("Ex. 010C, ATDP, 3E — réponse jusqu’au prompt ELM. "
           "Commandes constructeur à tes risques."),
        page);
    rawHint->setWordWrap(true);
    rawHint->setStyleSheet(QStringLiteral("color:#94a3b8; font-size:11px;"));
    root->addWidget(rawHint);

    auto* rawRow = new QHBoxLayout;
    m_rawCmdEdit = new QLineEdit(page);
    m_rawCmdEdit->setPlaceholderText(tr("Commande ELM…"));
    m_rawCmdEdit->setMinimumHeight(40);
    m_rawSendBtn = new QPushButton(tr("Envoyer"), page);
    m_rawSendBtn->setMinimumHeight(40);
    m_rawSendBtn->setObjectName(QStringLiteral("accentBtn"));
    connect(m_rawSendBtn, &QPushButton::clicked, this, &DriveWindow::sendRawDiagCommand);
    connect(m_rawCmdEdit, &QLineEdit::returnPressed, this, &DriveWindow::sendRawDiagCommand);
    rawRow->addWidget(m_rawCmdEdit, 1);
    rawRow->addWidget(m_rawSendBtn);
    root->addLayout(rawRow);

    m_rawLog = new QPlainTextEdit(page);
    m_rawLog->setReadOnly(true);
    m_rawLog->setMaximumBlockCount(200);
    m_rawLog->setMinimumHeight(120);
    m_rawLog->setPlaceholderText(tr("Réponses…"));
    m_rawLog->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background:#0b1220; color:#a7f3d0; font-family:monospace; font-size:12px; "
        "border:1px solid #334155; border-radius:8px; }"));
    root->addWidget(m_rawLog);

    // ── Codes défaut ────────────────────────────────────────────────────
    auto* title = new QLabel(tr("Codes défaut OBD"), page);
    title->setStyleSheet(QStringLiteral("color:#e6edf3; font-weight:700; font-size:16px;"));
    root->addWidget(title);

    m_dtcStatus = new QLabel(
        tr("Connecte un ELM327 puis appuie sur Lire DTC (modes 03 + 07)."), page);
    m_dtcStatus->setWordWrap(true);
    m_dtcStatus->setStyleSheet(QStringLiteral("color:#93c5fd; font-size:12px;"));
    root->addWidget(m_dtcStatus);

    auto* btnRow = new QHBoxLayout;
    m_dtcReadBtn = new QPushButton(tr("Lire DTC"), page);
    m_dtcClearBtn = new QPushButton(tr("Effacer DTC"), page);
    m_dtcReadBtn->setMinimumHeight(44);
    m_dtcClearBtn->setMinimumHeight(44);
    m_dtcReadBtn->setObjectName(QStringLiteral("accentBtn"));
    connect(m_dtcReadBtn, &QPushButton::clicked, this, &DriveWindow::readDtcs);
    connect(m_dtcClearBtn, &QPushButton::clicked, this, &DriveWindow::clearDtcs);
    btnRow->addWidget(m_dtcReadBtn, 1);
    btnRow->addWidget(m_dtcClearBtn, 1);
    root->addLayout(btnRow);

    auto* hdr = new QWidget(page);
    auto* hdrLay = new QHBoxLayout(hdr);
    hdrLay->setContentsMargins(8, 4, 8, 4);
    auto* hCode = new QLabel(tr("Code"), hdr);
    auto* hFam = new QLabel(tr("Famille"), hdr);
    auto* hSt = new QLabel(tr("Statut"), hdr);
    for (QLabel* l : {hCode, hFam, hSt})
        l->setStyleSheet(QStringLiteral("color:#94a3b8; font-size:11px; font-weight:600;"));
    hdrLay->addWidget(hCode, 2);
    hdrLay->addWidget(hFam, 2);
    hdrLay->addWidget(hSt, 3);
    root->addWidget(hdr);

    // Liste de lignes (pas de QTableWidget : plantage fréquent sur Android).
    m_dtcListHost = new QWidget(page);
    m_dtcListLay = new QVBoxLayout(m_dtcListHost);
    m_dtcListLay->setContentsMargins(0, 0, 0, 0);
    m_dtcListLay->setSpacing(6);
    root->addWidget(m_dtcListHost);

    m_dtcCopyBtn = new QPushButton(tr("Copier la liste"), page);
    m_dtcCopyBtn->setMinimumHeight(44);
    m_dtcCopyBtn->setEnabled(false);
    connect(m_dtcCopyBtn, &QPushButton::clicked, this, &DriveWindow::copyDtcs);
    root->addWidget(m_dtcCopyBtn);

    auto* hint = new QLabel(
        tr("Effacer remet le voyant moteur à zéro : corrige la cause avant, "
           "sinon le code revient."),
        page);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#f59e0b; font-size:12px;"));
    root->addWidget(hint);

    // ── Accès constructeur (Security Access / Seed-Key) ────────────────
    auto* saTitle = new QLabel(tr("Accès constructeur — Security Access"), page);
    saTitle->setStyleSheet(QStringLiteral("color:#e6edf3; font-weight:700; font-size:16px;"));
    root->addWidget(saTitle);

    auto* saInfo = new QLabel(
        tr("Certaines routines (actionneurs, codage, flash) nécessitent de déverrouiller "
           "l'ECU via le service 0x27 (UDS) ou 0x27/0x2781 (KWP2000). "
           "L'ECU envoie un seed aléatoire ; l'outil doit répondre avec la clé calculée "
           "dans les 5 secondes.\n\n"
           "PSA/Stellantis (Peugeot, Citroën, DS, Opel) : algorithme public (ludwig-v). "
           "Colle le seed hex ci-dessous avec la clé ECU (2 octets — extractible depuis "
           "les fichiers .cal ou bruteforce 65 536 combinaisons)."),
        page);
    saInfo->setWordWrap(true);
    saInfo->setStyleSheet(QStringLiteral("color:#94a3b8; font-size:12px;"));
    root->addWidget(saInfo);

    auto* saProtocol = new QComboBox(page);
    saProtocol->addItem(QStringLiteral("PSA / Stellantis (Peugeot, Citroën, DS, Opel)"), QStringLiteral("psa"));
    saProtocol->addItem(QStringLiteral("VAG (Volkswagen, Audi, Skoda, Seat) — SA2"), QStringLiteral("vag_sa2"));
    saProtocol->addItem(QStringLiteral("Générique XOR (OBD-II basique)"), QStringLiteral("xor"));
    saProtocol->setMinimumHeight(44);
    saProtocol->setStyleSheet(QStringLiteral(
        "QComboBox { background:#111827; color:#e6edf3; border:1px solid #334155; "
        "border-radius:8px; padding:6px 10px; font-size:13px; }"));
    root->addWidget(saProtocol);

    auto* saRow1 = new QHBoxLayout;
    m_saSeedEdit = new QLineEdit(page);
    m_saSeedEdit->setPlaceholderText(tr("Seed hex (ex: 5ADF35FE)"));
    m_saSeedEdit->setMinimumHeight(44);
    m_saSeedEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background:#111827; color:#e6edf3; border:1px solid #334155; "
        "border-radius:8px; padding:6px 10px; font-family:monospace; font-size:14px; }"));
    auto* saKeyLabel = new QLabel(tr("Clé ECU"), page);
    saKeyLabel->setStyleSheet(QStringLiteral("color:#94a3b8; font-size:12px;"));
    m_saEcuKeyEdit = new QLineEdit(page);
    m_saEcuKeyEdit->setPlaceholderText(tr("Clé ECU hex (ex: 50A6)"));
    m_saEcuKeyEdit->setMinimumHeight(44);
    m_saEcuKeyEdit->setStyleSheet(m_saSeedEdit->styleSheet());
    saRow1->addWidget(m_saSeedEdit, 2);
    saRow1->addWidget(saKeyLabel);
    saRow1->addWidget(m_saEcuKeyEdit, 1);
    root->addLayout(saRow1);

    auto* saCalcRow = new QHBoxLayout;
    auto* saCalcBtn = new QPushButton(tr("Calculer Key"), page);
    saCalcBtn->setMinimumHeight(44);
    saCalcBtn->setObjectName(QStringLiteral("accentBtn"));
    m_saResultLabel = new QLabel(QStringLiteral("—"), page);
    m_saResultLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_saResultLabel->setStyleSheet(QStringLiteral(
        "color:#34d399; font-weight:700; font-family:monospace; font-size:18px;"));
    m_saResultLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    saCalcRow->addWidget(saCalcBtn, 1);
    saCalcRow->addWidget(m_saResultLabel, 1);
    root->addLayout(saCalcRow);

    auto* saUnlockRow = new QHBoxLayout;
    auto* saUnlockBtn = new QPushButton(tr("Déverrouiller ECU (auto)"), page);
    saUnlockBtn->setMinimumHeight(44);
    saUnlockBtn->setToolTip(tr("Envoie la requête seed, calcule et envoie la key automatiquement via l'ELM connecté."));
    auto* saUnlockLevel = new QComboBox(page);
    saUnlockLevel->addItem(tr("Niveau 1 (download/flash)"), 1);
    saUnlockLevel->addItem(tr("Niveau 2 (config/zones)"), 2);
    saUnlockLevel->setMinimumHeight(44);
    saUnlockLevel->setStyleSheet(saProtocol->styleSheet());
    auto* saKwpChk = new QCheckBox(tr("KWP"), page);
    saKwpChk->setStyleSheet(QStringLiteral("color:#94a3b8; font-size:12px;"));
    saUnlockRow->addWidget(saUnlockBtn, 2);
    saUnlockRow->addWidget(saUnlockLevel, 1);
    saUnlockRow->addWidget(saKwpChk);
    root->addLayout(saUnlockRow);

    connect(saUnlockBtn, &QPushButton::clicked, this, [this, saProtocol, saUnlockLevel, saKwpChk]() {
        if (!m_elm || !m_connected) {
            if (m_rawLog) m_rawLog->appendPlainText(tr("[SA] ELM non connecté"));
            return;
        }
        const QString proto = saProtocol->currentData().toString();
        const QString ecuKeyHex = m_saEcuKeyEdit ? m_saEcuKeyEdit->text().trimmed().remove(QStringLiteral("0x")) : QString();
        bool keyOk = false;
        const quint16 ecuKey = ecuKeyHex.isEmpty() ? 0 : ecuKeyHex.toUShort(&keyOk, 16);
        const int level = saUnlockLevel->currentData().toInt();
        if (m_rawLog)
            m_rawLog->appendPlainText(tr("[SA] Requête seed %1 niveau %2...").arg(proto).arg(level));
        m_elm->sendSecurityAccessRequest(proto, level, ecuKey, saKwpChk->isChecked());
    });

    auto* saHint2 = new QLabel(
        tr("Commande KWP PSA : 2781 → seed ; 2782<KEY> → déverrouille download. "
           "2783/2784 pour config. Niveau UDS : 27 01 → seed ; 27 02 <KEY> → key.\n"
           "Lecture zone PSA : 21 XX (service 21). Écriture : 3B XX <data> (service 3B, ECU déverrouillé requis)."),
        page);
    saHint2->setWordWrap(true);
    saHint2->setStyleSheet(QStringLiteral("color:#64748b; font-size:11px;"));
    root->addWidget(saHint2);

    // ── Boutons rapides session KWP/UDS ────────────────────────────────
    auto* sessionTitle = new QLabel(tr("Session ECU — boutons rapides"), page);
    sessionTitle->setStyleSheet(QStringLiteral("color:#e6edf3; font-weight:700; font-size:14px; margin-top:8px;"));
    root->addWidget(sessionTitle);

    auto* sessionHint = new QLabel(
        tr("Séquence standard : Ouvrir session → Déverrouiller ECU (auto) → envoyer commandes → Keep-alive toutes les 2s → Fermer."),
        page);
    sessionHint->setWordWrap(true);
    sessionHint->setStyleSheet(QStringLiteral("color:#94a3b8; font-size:11px;"));
    root->addWidget(sessionHint);

    // Grille 2 colonnes de boutons session
    struct QuickBtn { const char* label; const char* cmd; const char* color; const char* tip; };
    const QuickBtn sessionBtns[] = {
        { "Session KWP",    "10C0",     "#3b82f6", "Ouvrir session étendue KWP (10 C0)" },
        { "Session UDS",    "1003",     "#3b82f6", "Ouvrir session diagnostic UDS (10 03)" },
        { "Keep-alive",     "3E00",     "#22c55e", "Tester présence (3E 00) — à envoyer ttes les 2s" },
        { "Reboot ECU",     "31A800",   "#f59e0b", "Redémarrer l'ECU via routine 31 A8 00" },
        { "Fermer session", "1001",     "#6b7280", "Fermer session / retour mode défaut (10 01)" },
        { "Lire DTC",       "190209",   "#a78bfa", "Lire DTC actifs UDS (19 02 09)" },
        { "Effacer DTC",    "14FFFFFF", "#f87171", "Effacer tous les DTC (14 FF FF FF)" },
        { "VIN",            "22F190",   "#34d399", "Lire VIN via UDS ReadDID (22 F1 90)" },
    };
    {
        auto* grid = new QGridLayout;
        grid->setSpacing(6);
        for (int i = 0; i < 8; ++i) {
            const QuickBtn& b = sessionBtns[i];
            auto* btn = new QPushButton(QString::fromUtf8(b.label), page);
            btn->setMinimumHeight(40);
            btn->setToolTip(QString::fromUtf8(b.tip));
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            btn->setStyleSheet(QStringLiteral(
                "QPushButton { background:%1; color:#fff; border-radius:6px; font-size:12px; font-weight:600; padding:4px 8px; }"
                "QPushButton:pressed { background:#1e293b; }").arg(QString::fromUtf8(b.color)));
            const QString cmd = QString::fromUtf8(b.cmd);
            connect(btn, &QPushButton::clicked, this, [this, cmd]() {
                if (!m_elm || !m_connected) {
                    if (m_rawLog) m_rawLog->appendPlainText(tr("[Diag] ELM non connecté"));
                    return;
                }
                if (m_rawLog) m_rawLog->appendPlainText(QStringLiteral("> %1").arg(cmd));
                m_elm->sendRawCommand(cmd);
            });
            grid->addWidget(btn, i / 2, i % 2);
        }
        root->addLayout(grid);
    }

    // ── Zones PSA préremplies ────────────────────────────────────────
    auto* zonesTitle = new QLabel(tr("Zones ECU — lecture rapide (service 21)"), page);
    zonesTitle->setStyleSheet(QStringLiteral("color:#e6edf3; font-weight:700; font-size:14px; margin-top:8px;"));
    root->addWidget(zonesTitle);

    struct ZoneBtn { const char* label; const char* zone; const char* tip; };
    const ZoneBtn zoneBtns[] = {
        { "Ident. ECU",   "8001",   "21 80 — ReadDataByLocalIdentifier zone 0x80" },
        { "Calibration",  "8601",   "21 86 — version calibration ECU" },
        { "Soft ECU",     "8701",   "21 87 — version logiciel ECU" },
        { "Hard ECU",     "8801",   "21 88 — version hardware ECU" },
        { "VIN",          "22F190", "22 F1 90 — UDS ReadDID numéro de série véhicule" },
        { "Coding BSI",   "A001",   "21 A0 — codage BSI" },
    };
    {
        auto* grid = new QGridLayout;
        grid->setSpacing(6);
        for (int i = 0; i < 6; ++i) {
            const ZoneBtn& z = zoneBtns[i];
            auto* btn = new QPushButton(QString::fromUtf8(z.label), page);
            btn->setMinimumHeight(38);
            btn->setToolTip(QString::fromUtf8(z.tip));
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            btn->setStyleSheet(QStringLiteral(
                "QPushButton { background:#1e3a5f; color:#93c5fd; border:1px solid #2563eb; border-radius:6px; font-size:12px; font-weight:600; padding:4px 8px; }"
                "QPushButton:pressed { background:#1e293b; }"));
            const QString cmd = QString::fromUtf8(z.zone);
            connect(btn, &QPushButton::clicked, this, [this, cmd]() {
                if (!m_elm || !m_connected) {
                    if (m_rawLog) m_rawLog->appendPlainText(tr("[Zone] ELM non connecté"));
                    return;
                }
                if (m_rawLog) m_rawLog->appendPlainText(QStringLiteral("> %1").arg(cmd));
                m_elm->sendRawCommand(cmd);
            });
            grid->addWidget(btn, i / 2, i % 2);
        }
        root->addLayout(grid);
    }

    // ── Actionneurs — boutons directs (EDC16 service 0x30) ────────────
    auto* actTitle2 = new QLabel(tr("Actionneurs EDC16 — IO Control (service 30)"), page);
    actTitle2->setStyleSheet(QStringLiteral("color:#e6edf3; font-weight:700; font-size:14px; margin-top:8px;"));
    root->addWidget(actTitle2);

    auto* actHint2 = new QLabel(
        tr("Séquence requise : Ouvrir session KWP → Déverrouiller ECU (niv. 2) → activer → Keep-alive → désactiver.\n"
           "Trame ON = 30 <LID> 07 FF  |  Trame OFF = 30 <LID> 00"),
        page);
    actHint2->setWordWrap(true);
    actHint2->setStyleSheet(QStringLiteral("color:#fbbf24; font-size:11px;"));
    root->addWidget(actHint2);

    struct ActQuickBtn { const char* label; const char* cmdOn; const char* cmdOff; };
    const ActQuickBtn actBtns[] = {
        { "EGR ON/OFF\n(LID 0x21)",       "302107FF", "302100" },
        { "Injecteur 1 stop\n(LID 0x10)",  "301007FF", "301000" },
        { "Injecteur 2 stop\n(LID 0x11)",  "301107FF", "301100" },
        { "Injecteur 3 stop\n(LID 0x12)",  "301207FF", "301200" },
        { "Injecteur 4 stop\n(LID 0x13)",  "301307FF", "301300" },
        { "Regen FAP rapide\n(LID 0x24)",   "302407FF", "302400" },
        { "Regen FAP full\n(LID 0x25)",     "302507FF", "302500" },
        { "Volet admission\n(LID 0x2B)",    "302B07FF", "302B00" },
    };

    // Grille 2 colonnes : chaque cellule = label + [ON] [OFF] sur une ligne
    {
        auto* grid = new QGridLayout;
        grid->setSpacing(6);
        for (int i = 0; i < 8; ++i) {
            const ActQuickBtn& b = actBtns[i];
            auto* cell = new QWidget(page);
            auto* cellLay = new QVBoxLayout(cell);
            cellLay->setContentsMargins(4, 4, 4, 4);
            cellLay->setSpacing(3);
            cell->setStyleSheet(QStringLiteral(
                "QWidget { background:#111827; border:1px solid #334155; border-radius:8px; }"));

            auto* lbl = new QLabel(QString::fromUtf8(b.label), cell);
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setWordWrap(true);
            lbl->setStyleSheet(QStringLiteral("color:#e2e8f0; font-size:11px; border:none; background:transparent;"));

            auto* btnRow2 = new QHBoxLayout;
            btnRow2->setSpacing(4);
            const QString cmdOn  = QString::fromUtf8(b.cmdOn);
            const QString cmdOff = QString::fromUtf8(b.cmdOff);
            auto* btnOn = new QPushButton(tr("ON"), cell);
            btnOn->setMinimumHeight(36);
            btnOn->setStyleSheet(QStringLiteral(
                "QPushButton { background:#166534; color:#fff; border-radius:6px; font-weight:700; border:none; }"
                "QPushButton:pressed { background:#14532d; }"));
            auto* btnOff = new QPushButton(tr("OFF"), cell);
            btnOff->setMinimumHeight(36);
            btnOff->setStyleSheet(QStringLiteral(
                "QPushButton { background:#7f1d1d; color:#fff; border-radius:6px; font-weight:700; border:none; }"
                "QPushButton:pressed { background:#450a0a; }"));
            connect(btnOn, &QPushButton::clicked, this, [this, cmdOn]() {
                if (!m_elm || !m_connected) { if (m_rawLog) m_rawLog->appendPlainText(tr("[Act] Non connecté")); return; }
                if (m_rawLog) m_rawLog->appendPlainText(QStringLiteral("> %1").arg(cmdOn));
                m_elm->sendRawCommand(cmdOn);
            });
            connect(btnOff, &QPushButton::clicked, this, [this, cmdOff]() {
                if (!m_elm || !m_connected) { if (m_rawLog) m_rawLog->appendPlainText(tr("[Act] Non connecté")); return; }
                if (m_rawLog) m_rawLog->appendPlainText(QStringLiteral("> %1").arg(cmdOff));
                m_elm->sendRawCommand(cmdOff);
            });
            btnRow2->addWidget(btnOn, 1);
            btnRow2->addWidget(btnOff, 1);
            cellLay->addWidget(lbl);
            cellLay->addLayout(btnRow2);
            grid->addWidget(cell, i / 2, i % 2);
        }
        root->addLayout(grid);
    }

    // Bouton routine PSA confirmées (service 0x31)
    auto* actTitle3 = new QLabel(tr("Routines PSA confirmées (service 31)"), page);
    actTitle3->setStyleSheet(QStringLiteral("color:#e6edf3; font-weight:700; font-size:14px; margin-top:8px;"));
    root->addWidget(actTitle3);

    struct RoutineBtn { const char* label; const char* cmdStart; const char* cmdStop; };
    const RoutineBtn routineBtns[] = {
        { "Reboot ECU\n(31 A8 00)", "31A800", nullptr },
        { "Reboot ECU 2\n(31 A8 01)", "31A801", nullptr },
        { "Flash autocontrol\n(37)", "37", nullptr },
    };
    {
        auto* grid = new QGridLayout;
        grid->setSpacing(6);
        int ri = 0;
        for (const RoutineBtn& r2 : routineBtns) {
            auto* btn = new QPushButton(QString::fromUtf8(r2.label), page);
            btn->setMinimumHeight(44);
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            btn->setStyleSheet(QStringLiteral(
                "QPushButton { background:#92400e; color:#fff; border-radius:6px; font-size:12px; font-weight:600; padding:4px 8px; }"
                "QPushButton:pressed { background:#78350f; }"));
            const QString cmd = QString::fromUtf8(r2.cmdStart);
            connect(btn, &QPushButton::clicked, this, [this, cmd]() {
                if (!m_elm || !m_connected) { if (m_rawLog) m_rawLog->appendPlainText(tr("[Routine] Non connecté")); return; }
                if (m_rawLog) m_rawLog->appendPlainText(QStringLiteral("> %1").arg(cmd));
                m_elm->sendRawCommand(cmd);
            });
            grid->addWidget(btn, ri / 2, ri % 2);
            ++ri;
        }
        root->addLayout(grid);
    }

    connect(saCalcBtn, &QPushButton::clicked, this, [this, saProtocol]() {
        const QString proto = saProtocol->currentData().toString();
        const QString seedHex = m_saSeedEdit->text().trimmed().remove(QStringLiteral("0x"));
        const QString ecuKeyHex = m_saEcuKeyEdit->text().trimmed().remove(QStringLiteral("0x"));

        bool seedOk = false, keyOk = false;
        const quint32 seed = seedHex.toUInt(&seedOk, 16);
        const quint16 ecuKey = ecuKeyHex.isEmpty() ? 0 : ecuKeyHex.toUShort(&keyOk, 16);

        if (!seedOk || seedHex.isEmpty()) {
            m_saResultLabel->setText(tr("Seed invalide"));
            m_saResultLabel->setStyleSheet(QStringLiteral("color:#f87171; font-weight:700; font-family:monospace; font-size:15px;"));
            return;
        }

        const auto algoOpt = ecu::SecurityAccess::fromName(proto.toStdString());
        if (!algoOpt) {
            m_saResultLabel->setText(tr("Algorithme inconnu"));
            m_saResultLabel->setStyleSheet(QStringLiteral("color:#f87171; font-weight:700; font-family:monospace; font-size:15px;"));
            return;
        }

        if (*algoOpt == ecu::SecurityAccess::Algo::PSA && ecuKeyHex.isEmpty()) {
            m_saResultLabel->setText(tr("Clé ECU requise pour PSA"));
            m_saResultLabel->setStyleSheet(QStringLiteral("color:#f87171; font-weight:700; font-family:monospace; font-size:15px;"));
            return;
        }

        const auto resultOpt = ecu::SecurityAccess::compute(*algoOpt, seed, ecuKey);
        if (!resultOpt) {
            m_saResultLabel->setText(tr("Calcul échoué"));
            m_saResultLabel->setStyleSheet(QStringLiteral("color:#f87171; font-weight:700; font-family:monospace; font-size:15px;"));
            return;
        }

        const QString keyHex = QString::number(*resultOpt, 16).toUpper()
                                   .rightJustified(8, QLatin1Char('0'));

        const auto& algos = ecu::SecurityAccess::algorithms();
        const auto it = std::find_if(algos.begin(), algos.end(),
            [&](const ecu::SecurityAccess::AlgoInfo& a){ return a.algo == *algoOpt; });
        const QString label = (it != algos.end())
            ? QString::fromStdString(std::string(it->name))
            : proto;

        m_saResultLabel->setStyleSheet(QStringLiteral("color:#34d399; font-weight:700; font-family:monospace; font-size:18px;"));
        m_saResultLabel->setText(QStringLiteral("%1").arg(keyHex));

        if (m_elm && m_connected) {
            const QString kwpFrame = QString::fromStdString(
                ecu::SecurityAccess::buildKwpKeyFrame(
                    ecu::SecurityAccess::KWP_KEY_CONFIG, *resultOpt));
            m_rawLog->appendPlainText(
                tr("[%1] Key calculée : %2  |  Trame KWP config : %3")
                    .arg(label, keyHex, kwpFrame));
        }
    });

    setupScrollablePage(scroll, page);
    return scroll;
}

void DriveWindow::setNavPage(int index) {
    auto applyNav = [](QPushButton* btn, bool on) {
        if (!btn) return;
        btn->setChecked(on);
        btn->setObjectName(on ? QStringLiteral("accentBtn") : QString());
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
        btn->update();
    };
    applyNav(m_driveNavBtn, index == 0);
    applyNav(m_sensNavBtn, index == 1);
    applyNav(m_diagNavBtn, index == 2);
}

void DriveWindow::showDrivePage() {
    setNavPage(0);
    forceStackPageRefresh(m_stack, 0);
    if (m_sessionOn) return;
    if (m_connected && m_elm)
        m_elm->stopPolling();
}

void DriveWindow::showSensorsPage() {
    if (!m_stack) return;
    setNavPage(1);
    forceStackPageRefresh(m_stack, 1);
    QTimer::singleShot(50, this, [this]() {
        if (!m_stack || m_stack->currentIndex() != 1) return;
        ensureSensorsPolling();
        refreshSensorsTable();
    });
}

void DriveWindow::showDiagPage() {
    if (!m_stack) return;
    setNavPage(2);
    forceStackPageRefresh(m_stack, 2);
    refreshDiagButtons();
    QTimer::singleShot(50, this, [this]() {
        if (!m_stack || m_stack->currentIndex() != 2) return;
        ensureTurboPolling();
        refreshTurboLive();
    });
}

QString DriveWindow::dtcFamily(const QString& code) const {
    if (code.isEmpty()) return QString();
    switch (code[0].toLatin1()) {
        case 'P': return tr("Powertrain");
        case 'C': return tr("Chassis");
        case 'B': return tr("Body");
        case 'U': return tr("Network");
        default:  return QStringLiteral("?");
    }
}

QString DriveWindow::dtcStatusText(int flags) const {
    if ((flags & kDtcStored) && (flags & kDtcPending))
        return tr("mémorisé + en attente");
    if (flags & kDtcPending) return tr("en attente");
    if (flags & kDtcStored)  return tr("mémorisé");
    return QStringLiteral("—");
}

void DriveWindow::mergeDtcCodes(const QStringList& codes, bool pending) {
    const int bit = pending ? kDtcPending : kDtcStored;
    for (const QString& c : codes)
        m_dtcFlags[c] = m_dtcFlags.value(c, 0) | bit;
}

void DriveWindow::refreshDtcList() {
    if (!m_dtcListLay) return;
    while (QLayoutItem* it = m_dtcListLay->takeAt(0)) {
        if (QWidget* w = it->widget())
            w->deleteLater();
        delete it;
    }
    QStringList keys = m_dtcFlags.keys();
    keys.sort();
    for (const QString& code : keys) {
        auto* row = new QWidget(m_dtcListHost);
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(8, 8, 8, 8);
        row->setStyleSheet(QStringLiteral(
            "QWidget { background:#111827; border:1px solid #334155; border-radius:8px; }"));

        auto* c = new QLabel(code, row);
        c->setStyleSheet(QStringLiteral(
            "color:#f8fafc; font-weight:700; font-size:15px; border:none; background:transparent;"));
        auto* f = new QLabel(dtcFamily(code), row);
        f->setStyleSheet(QStringLiteral(
            "color:#93c5fd; border:none; background:transparent;"));
        auto* s = new QLabel(dtcStatusText(m_dtcFlags.value(code)), row);
        s->setWordWrap(true);
        s->setStyleSheet(QStringLiteral(
            "color:#e2e8f0; border:none; background:transparent;"));

        hl->addWidget(c, 2);
        hl->addWidget(f, 2);
        hl->addWidget(s, 3);
        m_dtcListLay->addWidget(row);
    }
    if (m_dtcCopyBtn)
        m_dtcCopyBtn->setEnabled(!keys.isEmpty());
}

void DriveWindow::refreshDiagButtons() {
    const bool on = m_connected && m_elm;
    if (m_dtcReadBtn) m_dtcReadBtn->setEnabled(on && m_dtcAwaiting == 0 && !m_dtcClearPending);
    if (m_dtcClearBtn) m_dtcClearBtn->setEnabled(on && m_dtcAwaiting == 0 && !m_dtcClearPending);
    if (m_dtcCopyBtn) m_dtcCopyBtn->setEnabled(!m_dtcFlags.isEmpty());
    if (m_rawSendBtn) m_rawSendBtn->setEnabled(on);
    if (m_rawCmdEdit) m_rawCmdEdit->setEnabled(on);
    if (m_dtcStatus && !on && m_dtcAwaiting == 0 && !m_dtcClearPending) {
        m_dtcStatus->setText(
            tr("Hors ligne — connecte un ELM327 pour lire / effacer les DTC."));
    }
    if (m_turboStatus && !on) {
        m_turboStatus->setText(
            tr("Hors ligne — connecte un ELM327 pour le live turbo."));
    }
}

void DriveWindow::resumePollingAfterDtc() {
    if (!m_connected || !m_elm) return;
    if (m_sessionOn) {
        const auto pids = m_validator.requiredPids();
        QList<std::uint8_t> qp;
        for (auto p : pids) qp.append(p);
        if (qp.isEmpty()) qp = { 0x0C, 0x04, 0x0B, 0x33 };
        m_elm->startPolling(qp, 180);
        return;
    }
    if (m_stack && m_stack->currentIndex() == 1)
        ensureSensorsPolling();
    else if (m_stack && m_stack->currentIndex() == 2)
        ensureTurboPolling();
}

void DriveWindow::readDtcs() {
    if (!m_connected || !m_elm) {
        platformToast(tr("Connecte l'ELM d'abord"));
        return;
    }
    if (m_dtcAwaiting > 0 || m_dtcClearPending) return;
    m_elm->stopPolling();
    m_dtcFlags.clear();
    refreshDtcList();
    m_dtcAwaiting = 2;
    refreshDiagButtons();
    if (m_dtcStatus)
        m_dtcStatus->setText(tr("Lecture DTC modes 03 + 07…"));
    setStatus(tr("Lecture DTC modes 03 + 07…"));
    platformToast(tr("Lecture DTC…"));
    m_elm->readDtcs(false);
    m_elm->readDtcs(true);
}

void DriveWindow::clearDtcs() {
    if (!m_connected || !m_elm) {
        platformToast(tr("Connecte l'ELM d'abord"));
        return;
    }
    if (m_dtcAwaiting > 0 || m_dtcClearPending) return;
    showInfoDialog(
        tr("Effacer les codes défaut ?"),
        tr("Mode OBD 04 : efface les DTC mémorisés et peut éteindre le voyant moteur.\n\n"
           "Corrige d'abord la cause, sinon les codes reviennent.\n"
           "Contact / contact coupé peut être demandé par certains ECU."),
        tr("Annuler"),
        tr("Effacer"),
        [this]() {
            if (!m_connected || !m_elm) return;
            m_elm->stopPolling();
            m_dtcClearPending = true;
            refreshDiagButtons();
            if (m_dtcStatus)
                m_dtcStatus->setText(tr("Effacement DTC (mode 04)…"));
            setStatus(tr("Effacement DTC…"));
            platformToast(tr("Effacement DTC…"));
            m_elm->clearDtcs();
        });
}

void DriveWindow::copyDtcs() {
    QStringList lines;
    QStringList keys = m_dtcFlags.keys();
    keys.sort();
    for (const QString& code : keys)
        lines << QStringLiteral("%1\t%2\t%3")
                     .arg(code, dtcFamily(code), dtcStatusText(m_dtcFlags.value(code)));
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    setStatus(tr("%1 code(s) copié(s)").arg(lines.size()));
    platformToast(tr("%1 DTC copié(s)").arg(lines.size()));
}

void DriveWindow::ensureTurboPolling() {
    if (!m_connected || !m_elm) {
        if (m_turboStatus)
            m_turboStatus->setText(tr("Hors ligne — connecte un ELM327 d'abord."));
        return;
    }
    if (m_sessionOn) {
        if (m_turboStatus)
            m_turboStatus->setText(tr("Session conduite active — PID de validation (même bus)."));
        refreshTurboLive();
        return;
    }
    if (m_dtcAwaiting > 0 || m_dtcClearPending) return;
    // MAP, baro, MAF, RPM, charge — suffisant pour juger wastegate / underboost.
    m_elm->startPolling({ 0x0B, 0x33, 0x10, 0x0C, 0x04 }, 180);
    if (m_turboStatus)
        m_turboStatus->setText(tr("Live turbo — MAP / baro / MAF / régime."));
    refreshTurboLive();
}

void DriveWindow::refreshTurboLive() {
    auto fmt = [](bool ok, const QString& text) {
        return ok ? text : QStringLiteral("—");
    };
    const bool hasMap = m_live.contains(0x0B);
    const bool hasBaro = m_live.contains(0x33);
    const bool hasMaf = m_live.contains(0x10);
    const bool hasRpm = m_live.contains(0x0C);

    const double mapMbar = hasMap
        ? ecu::TuneValidator::mapAbsKpaToMbar(m_live.value(0x0B)) : 0.0;
    const double baroMbar = hasBaro
        ? ecu::TuneValidator::mapAbsKpaToMbar(m_live.value(0x33)) : 0.0;

    if (m_turboMap)
        m_turboMap->setText(fmt(hasMap, tr("%1 mbar").arg(mapMbar, 0, 'f', 0)));
    if (m_turboBaro)
        m_turboBaro->setText(fmt(hasBaro, tr("%1 mbar").arg(baroMbar, 0, 'f', 0)));
    if (m_turboDelta) {
        if (hasMap && hasBaro) {
            const double d = mapMbar - baroMbar;
            m_turboDelta->setText(tr("%1 mbar").arg(d, 0, 'f', 0));
            m_turboDelta->setStyleSheet(QStringLiteral(
                "color:%1; font-weight:700; font-size:16px; border:none; background:transparent;")
                .arg(d >= 150.0 ? QStringLiteral("#34d399")
                     : d >= 50.0 ? QStringLiteral("#fbbf24")
                                 : QStringLiteral("#f87171")));
        } else {
            m_turboDelta->setText(QStringLiteral("—"));
            m_turboDelta->setStyleSheet(QStringLiteral(
                "color:#60a5fa; font-weight:700; font-size:16px; border:none; background:transparent;"));
        }
    }
    if (m_turboMaf)
        m_turboMaf->setText(fmt(hasMaf, tr("%1 g/s").arg(m_live.value(0x10), 0, 'f', 1)));
    if (m_turboRpm)
        m_turboRpm->setText(fmt(hasRpm, tr("%1 tr/min").arg(m_live.value(0x0C), 0, 'f', 0)));
}

void DriveWindow::sendRawDiagCommand() {
    if (!m_connected || !m_elm) {
        platformToast(tr("Connecte l'ELM d'abord"));
        return;
    }
    if (!m_rawCmdEdit) return;
    const QString cmd = m_rawCmdEdit->text().trimmed();
    if (cmd.isEmpty()) return;
    if (m_rawLog)
        m_rawLog->appendPlainText(QStringLiteral("> %1").arg(cmd));
    m_elm->sendRawCommand(cmd);
    setStatus(tr("Commande brute : %1").arg(cmd));
}

void DriveWindow::onRawResponse(const QString& command, const QString& response) {
    if (!m_rawLog) return;
    QString body = response.trimmed();
    if (body.isEmpty()) body = QStringLiteral("(vide)");
    m_rawLog->appendPlainText(QStringLiteral("[%1]\n%2").arg(command, body));
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
                         "Android 12+ : Autorisations → Appareils à proximité = Autorisé.\n"
                         "Android plus ancien : Position (pour le scan BT)."),
                      true);
            platformToast(tr("Permission BT refusée"));
            showInfoDialog(
                tr("Bluetooth"),
                tr("Sans permission Bluetooth, le scan et la connexion ELM "
                   "sont impossibles.\n\n"
                   "Android 12 et plus :\n"
                   "Réglages → Applications → ECU Drive → Autorisations "
                   "→ Appareils à proximité → Autoriser.\n\n"
                   "Android 11 et moins : autorise aussi la Position "
                   "(requise pour découvrir le dongle)."),
                tr("OK"),
                tr("Ouvrir les réglages"),
                []() { platformOpenAppSettings(); });
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
                setStatus(tr("Permission Bluetooth refusée.\n"
                             "Android 12+ : Autorisations → Appareils à proximité."),
                          true);
                platformToast(tr("Permission BT refusée"));
                showInfoDialog(
                    tr("Bluetooth"),
                    tr("Active « Appareils à proximité » (ou Position sur Android 11−) "
                       "pour ECU Drive."),
                    tr("OK"),
                    tr("Ouvrir les réglages"),
                    []() { platformOpenAppSettings(); });
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
    platformStartLoggingService(
        tr("ECU Drive"),
        tr("Session conduite — logging OBD actif"));
    m_verdict->setText(tr("Acquisition…"));
    endBusy();
    setStatus(tr("Session conduite active — %1 PID(s).").arg(qp.size()));
}

void DriveWindow::stopSession() {
    if (!m_sessionOn) return;
    beginBusy(tr("Arrêt de la session…"));
    m_elm->stopPolling();
    m_sessionOn = false;
    platformStopLoggingService();
    autoStopCsv();
    const auto sum = m_session.finish();
    m_sessionBtn->setText(tr("▶  Lancer session conduite"));
    refreshSessionButton();
    endBusy();
    if (sum.ticks > 0) showSummary(sum);
    else setStatus(tr("Session arrêtée (aucune donnée)."));
    if (m_stack && m_stack->currentIndex() == 1)
        ensureSensorsPolling();
    else if (m_stack && m_stack->currentIndex() == 2)
        ensureTurboPolling();
}

void DriveWindow::onPid(quint8 pid, double value, const QString&, const QString& unit) {
    m_live[pid] = value;
    if (!unit.isEmpty())
        m_liveUnit[pid] = unit;
    if (m_sessionOn) runValidation();
    if (!m_uiSuspended && m_stack && m_stack->currentIndex() == 1)
        refreshSensorsTable();
    if (!m_uiSuspended && m_stack && m_stack->currentIndex() == 2)
        refreshTurboLive();
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
    appendCsv(results);

    if (m_uiSuspended) {
        // Pas de paint : avancer l'hystérésis + bip underboost seulement.
        if (const auto boost = primaryBoost(results)) {
            const auto shown = m_hyst.update(boost->status);
            if (shown == ecu::ValidationStatus::Fail)
                maybeAlert();
        }
        return;
    }
    updateDriveUi(results);
    refreshMapsList(results);
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
    if (!m_mapsListLay) return;
    while (QLayoutItem* it = m_mapsListLay->takeAt(0)) {
        if (QWidget* w = it->widget())
            w->deleteLater();
        delete it;
    }
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
        auto* row = new QLabel(line, m_mapsListHost);
        row->setWordWrap(true);
        QString color = QStringLiteral("#4ade80");
        if (r.status == ecu::ValidationStatus::Fail)
            color = QStringLiteral("#f87171");
        else if (r.status == ecu::ValidationStatus::Warn)
            color = QStringLiteral("#fbbf24");
        row->setStyleSheet(QStringLiteral(
            "QLabel { background:#111827; border:1px solid #334155; border-radius:8px; "
            "color:%1; font-size:12px; padding:8px; }").arg(color));
        m_mapsListLay->addWidget(row);
        if (++shown >= 8) break;
    }
    if (shown == 0) {
        auto* empty = new QLabel(tr("En attente de données map…"), m_mapsListHost);
        empty->setWordWrap(true);
        empty->setStyleSheet(QStringLiteral(
            "QLabel { background:#111827; border:1px solid #334155; border-radius:8px; "
            "color:#94a3b8; font-size:12px; padding:8px; }"));
        m_mapsListLay->addWidget(empty);
    }
}

void DriveWindow::maybeAlert() {
    const int streak = m_hyst.failStreak();
    if (streak < 3 || streak == m_lastAlertAt || streak % 3 != 0) return;
    m_lastAlertAt = streak;
    if (m_beepChk && m_beepChk->isChecked())
        platformAlertBeep();
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
    m_csvMaps.clear();
    m_lastCsvWriteMs = 0;

    QTextStream ts(m_csv);
    ts << QStringLiteral("# ecu-drive-session v2\n");
    ts << QStringLiteral("# app=") << QStringLiteral(APP_VERSION)
       << QStringLiteral(" ecu=") << m_validator.ecuId()
       << QStringLiteral(" rom_md5=") << m_validator.romMd5()
       << QStringLiteral(" started=")
       << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
    ts << QStringLiteral("# format=wide — 1 ligne = 1 échantillon live (rpm/load/map + maps)\n");
    ts << QStringLiteral("# status: OK | Attention | Ecart | (vide=pas de donnee)\n");

    QString header = QStringLiteral("time,rpm,load_pct,speed_kmh,map_mbar,maf_gs");
    for (const auto& rule : m_validator.rules()) {
        if (!rule.enabled) continue;
        const QString name = QString::fromStdString(rule.mapName);
        m_csvMaps.append(name);
        const QString col = csvSanitizeMapName(name);
        header += QStringLiteral(",%1_meas,%1_exp,%1_delta,%1_status,%1_unit")
                      .arg(col);
    }
    ts << header << '\n';
    m_csv->flush();

    m_csvLabel->setText(tr("CSV : %1").arg(QFileInfo(m_lastCsv).fileName()));
    setStatus(tr("Log CSV en cours — enregistrement sous à la fin"));
    platformToast(tr("Log CSV démarré"));
}

void DriveWindow::autoStopCsv() {
    if (!m_csv) return;
    {
        QTextStream ts(m_csv);
        const auto& c = m_session.current();
        ts << QStringLiteral("# summary ticks=") << c.ticks
           << QStringLiteral(" ok=") << c.ok
           << QStringLiteral(" warn=") << c.warn
           << QStringLiteral(" fail=") << c.fail
           << QStringLiteral(" nodata=") << c.noData
           << QStringLiteral(" ok_pct=") << QString::number(c.okRatio(), 'f', 1)
           << QStringLiteral(" peak_abs_delta=") << QString::number(c.peakAbsDelta, 'f', 1)
           << QStringLiteral(" peak_map=") << c.peakMap << '\n';
    }
    m_csv->flush();
    m_csv->close();
    m_csv->deleteLater();
    m_csv = nullptr;
    m_csvMaps.clear();
    m_csvLabel->setText(tr("CSV : %1 (terminé)").arg(QFileInfo(m_lastCsv).fileName()));
}

QString DriveWindow::csvSanitizeMapName(const QString& mapName) {
    QString s = mapName;
    for (QChar& ch : s) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('_'))
            ch = QLatin1Char('_');
    }
    return s;
}

void DriveWindow::appendCsv(const std::vector<ecu::ValidationResult>& results) {
    if (!m_csv) return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Rate-limit : une ligne toutes les 250 ms max (évite 1 ligne / PID).
    if (m_lastCsvWriteMs > 0 && (now - m_lastCsvWriteMs) < 250)
        return;

    const double rpm = m_live.value(0x0C, 0.0);
    const double load = m_live.value(0x04, 0.0);
    const double speed = m_live.value(0x0D, 0.0);
    const double mapKpa = m_live.value(0x0B, 0.0);
    const double maf = m_live.value(0x10, 0.0);

    bool anyMap = false;
    QHash<QString, const ecu::ValidationResult*> byName;
    byName.reserve(static_cast<int>(results.size()));
    for (const auto& r : results) {
        byName.insert(r.mapName, &r);
        if (r.status != ecu::ValidationStatus::NoData)
            anyMap = true;
    }

    // Ignore les ticks sans RPM ni mesure utile (démarrage / perte liaison).
    if (rpm < 200.0 && !anyMap)
        return;

    m_lastCsvWriteMs = now;
    QTextStream ts(m_csv);
    const QString t = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    auto numOrEmpty = [](double v, bool ok, int prec) -> QString {
        return ok ? QString::number(v, 'f', prec) : QString();
    };

    ts << t << ','
       << numOrEmpty(rpm, rpm > 0.0, 0) << ','
       << numOrEmpty(load, load > 0.0 || rpm > 0.0, 1) << ','
       << numOrEmpty(speed, m_live.contains(0x0D), 0) << ','
       << numOrEmpty(ecu::TuneValidator::mapAbsKpaToMbar(mapKpa), m_live.contains(0x0B), 0) << ','
       << numOrEmpty(maf, m_live.contains(0x10), 2);

    for (const QString& mapName : m_csvMaps) {
        const ecu::ValidationResult* r = byName.value(mapName, nullptr);
        if (!r || r->status == ecu::ValidationStatus::NoData) {
            ts << QStringLiteral(",,,,,");
            continue;
        }
        ts << ',' << QString::number(r->measured, 'f', 2)
           << ',' << QString::number(r->expected, 'f', 2)
           << ',' << QString::number(r->delta, 'f', 2)
           << ',' << statusLabel(r->status)
           << ',' << r->unit;
    }
    ts << '\n';
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

#if defined(Q_OS_ANDROID)
    // MediaStore → Téléchargements : pas besoin de WRITE sur Android 10+.
    const QString media = platformSaveToDownloads(sourceCsv, name);
    if (!media.isEmpty()) {
        m_lastCsv = media;
        m_session.setCsvPath(media);
        m_csvLabel->setText(tr("CSV : %1").arg(name));
        setStatus(tr("Log enregistré dans Téléchargements :\n%1").arg(name));
        platformToast(tr("Log dans Téléchargements"));
        return media;
    }
#endif

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

    // Pulse UI pendant le DL : sous Qt Android, les setValue() ne se voient
    // souvent qu'après un clic (d'où « bouge seulement si Vérifier MAJ »).
    if (st == S::Downloading) {
        if (!m_updateUiPulse) {
            m_updateUiPulse = new QTimer(this);
            m_updateUiPulse->setInterval(250);
            connect(m_updateUiPulse, &QTimer::timeout, this, [this]() {
                if (m_updater && m_updater->state() == Updater::Downloading)
                    refreshUpdateBanner();
                else if (m_updateUiPulse)
                    m_updateUiPulse->stop();
            });
        }
        if (!m_updateUiPulse->isActive())
            m_updateUiPulse->start();
    } else if (m_updateUiPulse) {
        m_updateUiPulse->stop();
    }

    m_updateBanner->setVisible(show);
    if (!show) return;

    m_updateProgress->setVisible(st == S::Downloading);
    if (st == S::Downloading && m_updater->progressIndeterminate()) {
        m_updateProgress->setRange(0, 0);
        m_updateProgress->setValue(0);
    } else {
        m_updateProgress->setRange(0, 100);
        m_updateProgress->setValue(int(m_updater->progress() * 100.0 + 0.5));
    }
    m_updateActionBtn->setVisible(st != S::Downloading);
    m_updateDismissBtn->setVisible(st != S::Downloading);

    if (st == S::Downloading) {
        const QString label = m_updater->progressLabel();
        m_updateTitle->setText(tr("Téléchargement %1…").arg(m_updater->latestVersion()));
        m_updateSub->setText(tr("%1 — ne ferme pas l'app").arg(label));
        setStatus(tr("Téléchargement %1 — %2")
                      .arg(m_updater->latestVersion(), label));
        m_updateSub->repaint();
        m_updateProgress->repaint();
        m_updateBanner->repaint();
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
