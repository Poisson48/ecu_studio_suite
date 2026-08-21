#include "drive_controller.h"
#include "updater.h"
#include "platform.h"

#include "elm/Elm327.hpp"
#include "ecu/Obd2.hpp"
#include "ecu/SecurityAccess.hpp"
#include "ecu/ActuatorControl.hpp"
#include "ecu/TunePackage.hpp"
#include "ecu/OpenDamos.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QtMath>

#include <algorithm>

#if defined(Q_OS_ANDROID)
#  include <QFileDialog>
#  include <QJniObject>
#  include <QJniEnvironment>
#endif
#if defined(ELM_HAVE_BLUETOOTH)
#  include <QBluetoothLocalDevice>
#  include <QBluetoothAddress>
#endif

namespace ecu_drive {

// ── Helpers locaux ────────────────────────────────────────────────────────────

static QString statusLabel(ecu::ValidationStatus s) {
    switch (s) {
        case ecu::ValidationStatus::Ok:     return QStringLiteral("OK");
        case ecu::ValidationStatus::Warn:   return QStringLiteral("Attention");
        case ecu::ValidationStatus::Fail:   return QStringLiteral("Ecart");
        default:                            return {};
    }
}

// ── Constructeur ──────────────────────────────────────────────────────────────

DriveController::DriveController(elm::Elm327* elm, Updater* updater, QObject* parent)
    : QObject(parent), m_elm(elm), m_updater(updater)
{
    Q_ASSERT(elm);
    Q_ASSERT(updater);

    m_btObdOnly = QSettings().value(QStringLiteral("drive/btObdOnly"), true).toBool();
    m_beepAlert = QSettings().value(QStringLiteral("drive/beepAlert"), true).toBool();
    m_selectedBt = QSettings().value(QStringLiteral("drive/lastBt")).toString();
    m_selectedPort = QSettings().value(QStringLiteral("drive/lastUsbPort")).toString();

    connect(m_elm, &elm::Elm327::connected, this, [this](const QString& v) {
        m_connected = true;
        m_linkLossNotified = false;
        m_pendingDisconnectReason.clear();
        m_ecuSupportedPids.clear();
        m_knownUnsupported.clear();
        m_supportProbed = false;
        emit connectedChanged();
        setStatus(tr("Connecté — %1").arg(v));
        emit toast(tr("ELM connecté — prêt"));
        // Découvre les PID supportés puis démarre le polling adapté à l'onglet.
        m_elm->probeSupportedPids();
    });

    connect(m_elm, &elm::Elm327::disconnected, this, [this]() {
        const bool intentional = m_userDisconnect;
        m_userDisconnect = false;
        m_connected = false;
        m_ecuSupportedPids.clear();
        m_knownUnsupported.clear();
        m_supportProbed = false;
        m_live.clear();
        m_liveUnit.clear();
        refreshSensorsTable();
        refreshTurboLive();
        const bool hadSession = m_sessionOn;
        if (!intentional && hadSession && m_pendingDisconnectReason.isEmpty())
            m_pendingDisconnectReason = tr("Liaison Bluetooth perdue.");
        if (m_sessionOn) stopSession();
        emit connectedChanged();
        setStatus(tr("Déconnecté."));
        if (m_linkLossNotified) {
            m_linkLossNotified = false;
            if (!hadSession && !m_pendingDisconnectReason.isEmpty())
                emit showDialog(tr("Connexion"), m_pendingDisconnectReason, tr("OK"));
            m_pendingDisconnectReason.clear();
            return;
        }
        if (hadSession) return;
        if (intentional)
            emit showDialog(tr("Déconnecté"),
                tr("Module déconnecté.\nTu peux reconnecter quand tu veux."), tr("OK"));
        else
            emit showDialog(tr("Module déconnecté"),
                tr("La liaison avec le dongle a été perdue.\n\n"
                   "Vérifie l'alimentation / la portée Bluetooth."), tr("OK"));
    });

    connect(m_elm, &elm::Elm327::errorOccurred, this, [this](const QString& e) {
        m_linkLossNotified = true;
        m_pendingDisconnectReason = e;
        m_connected = false;
        const bool hadSession = m_sessionOn;
        if (m_sessionOn) stopSession();
        emit connectedChanged();
        setStatus(e, true);
        emit toast(e.split(QLatin1Char('\n')).first());
        if (!hadSession)
            emit showDialog(tr("Connexion"), e, tr("OK"));
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
        setStatus(ok ? tr("Codes effacés — relis pour confirmer.") : s);
        emit connectedChanged(); // refreshe dtcClearEnabled
        applyPollingForCurrentPage();
    });

    connect(m_elm, &elm::Elm327::pidResult,    this, &DriveController::onPid);
    connect(m_elm, &elm::Elm327::rawResponse,  this, &DriveController::onRawResponse);

    connect(m_elm, &elm::Elm327::supportedPidsReady, this, [this](const QList<quint8>& pids) {
        m_ecuSupportedPids = QSet<quint8>(pids.begin(), pids.end());
        m_supportProbed = true;
        // Retire de la blacklist ce que le bitmap déclare supporté.
        for (quint8 p : pids)
            m_knownUnsupported.remove(p);
        applyPollingForCurrentPage();
        refreshSensorsTable();
    });

    connect(m_elm, &elm::Elm327::pidUnsupported, this, [this](quint8 pid) {
        if (m_knownUnsupported.contains(pid))
            return;
        m_knownUnsupported.insert(pid);
        refreshSensorsTable();
        // Debounce : un cycle de poll peut émettre plusieurs NO DATA d'affilée.
        if (m_pollRebuildPending)
            return;
        m_pollRebuildPending = true;
        QTimer::singleShot(50, this, [this]() {
            m_pollRebuildPending = false;
            if (m_connected)
                applyPollingForCurrentPage();
        });
    });

    connect(m_elm, &elm::Elm327::securityAccessResult, this, [this](bool ok, const QString& keyHex, const QString& detail) {
        m_saResult = ok ? tr("✓ key=%1 — %2").arg(keyHex, detail) : tr("✗ %1").arg(detail);
        emit saResultChanged();
        appendRawLog(QStringLiteral("[SA] %1").arg(m_saResult));
    });

    connect(m_elm, &elm::Elm327::dtcsReady, this, [this](const QStringList& codes, bool pending) {
        mergeDtcCodes(codes, pending);
        if (m_dtcAwaiting > 0) --m_dtcAwaiting;
        if (m_dtcAwaiting == 0) {
            setStatus(tr("%1 code(s) défaut trouvé(s).").arg(m_dtcFlags.size()));
            applyPollingForCurrentPage();
        }
    });

    // Chargement du dernier tune au démarrage
    QTimer::singleShot(300, this, [this]() {
        const QString lastTune = QSettings().value(QStringLiteral("drive/lastTune")).toString();
        if (!lastTune.isEmpty() && QFile::exists(lastTune)) {
            m_suppressEcuPromptOnce = true;
            loadTuneFile(lastTune);
        }
        refreshPorts();
#if defined(ELM_HAVE_BLUETOOTH)
        selectLastBtDevice();
        platformRequestBluetoothPermissions([this](bool granted) {
            if (granted) loadBondedBtDevices();
        });
#endif
    });

    QTimer::singleShot(1500, m_updater, &Updater::check);

    // App state — en session, réaffirme le FGS + wake quand on passe en arrière-plan.
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState st) {
        const bool wasSuspended = m_uiSuspended;
        m_uiSuspended = (st == Qt::ApplicationInactive
                         || st == Qt::ApplicationSuspended
                         || st == Qt::ApplicationHidden);
        if (!m_sessionOn)
            return;
        if (m_uiSuspended && !wasSuspended) {
            // App quitte le premier plan : s'assurer que le service + wake lock tiennent.
            platformStartLoggingService(
                tr("ECU Drive"),
                tr("Logging OBD actif (arrière-plan)"));
            ensureBackgroundWatchdog(true);
        } else if (!m_uiSuspended && wasSuspended) {
            platformUpdateLoggingService(
                tr("ECU Drive"),
                tr("Session conduite — logging OBD actif"));
        }
    });
}
DriveController::~DriveController() = default;

// ── Propriétés ────────────────────────────────────────────────────────────────

QVariantList DriveController::btDevices() const {
#if defined(ELM_HAVE_BLUETOOTH)
    QVariantList out;
    for (const BtDevice& d : m_btDevices) {
        QVariantMap m;
        m[QStringLiteral("addr")] = d.addr;
        m[QStringLiteral("name")] = d.name;
        m[QStringLiteral("likelyObd")] = d.likelyObd;
        out.append(m);
    }
    return out;
#else
    return {};
#endif
}

void DriveController::setSelectedBt(const QString& addr) {
    if (m_selectedBt == addr) return;
    m_selectedBt = addr;
    QSettings().setValue(QStringLiteral("drive/lastBt"), addr);
    emit selectedBtChanged();
}

void DriveController::setBtObdOnly(bool on) {
    if (m_btObdOnly == on) return;
    m_btObdOnly = on;
    QSettings().setValue(QStringLiteral("drive/btObdOnly"), on);
    emit btObdOnlyChanged();
    emit btDevicesChanged();
}

void DriveController::setSelectedPort(const QString& port) {
    if (m_selectedPort == port) return;
    m_selectedPort = port;
    QSettings().setValue(QStringLiteral("drive/lastUsbPort"), port);
    emit selectedPortChanged();
}

void DriveController::setBeepAlert(bool on) {
    if (m_beepAlert == on) return;
    m_beepAlert = on;
    QSettings().setValue(QStringLiteral("drive/beepAlert"), on);
    emit beepAlertChanged();
}

// ── Status / Busy ─────────────────────────────────────────────────────────────

void DriveController::setStatus(const QString& msg, bool error) {
#if defined(ELM_HAVE_BLUETOOTH)
    if (m_btScanning && !msg.contains(QStringLiteral("Scan")))
        return;
#endif
    m_statusText  = msg;
    m_statusError = error;
    emit statusChanged();
}

void DriveController::beginBusy(const QString& msg, int max) {
    ++m_busyDepth;
    m_busyLabel = msg;
    m_busyMax   = max;
    m_busyValue = 0;
    emit busyChanged();
}

void DriveController::setBusy(int value, const QString& msg) {
    m_busyValue = value;
    if (!msg.isEmpty()) m_busyLabel = msg;
    emit busyChanged();
}

void DriveController::endBusy() {
    if (m_busyDepth > 0) --m_busyDepth;
    emit busyChanged();
}

// ── Connexion ─────────────────────────────────────────────────────────────────

void DriveController::refreshPorts() {
    const QString keep = m_selectedPort;
    m_ports.clear();
#if !defined(Q_OS_ANDROID)
    for (const auto& p : elm::Elm327::listPorts()) {
        const QString label = (p.likelyElm ? QStringLiteral("★ ") : QString())
                              + p.port + QStringLiteral(" — ") + p.description;
        m_ports.append(label + QLatin1Char('|') + p.port);
    }
#endif
    QString chosen;
    if (!keep.isEmpty()) {
        for (const QString& item : m_ports) {
            const QString port = item.section(QLatin1Char('|'), 1, 1);
            if (port == keep) { chosen = port; break; }
        }
    }
    if (chosen.isEmpty() && !m_ports.isEmpty())
        chosen = m_ports.first().section(QLatin1Char('|'), 1, 1);
    setSelectedPort(chosen);
    emit portsChanged();
}

void DriveController::startBtScan() {
#if defined(ELM_HAVE_BLUETOOTH)
    if (m_btScanning) {
        // toggle stop
        if (m_btAgent) m_btAgent->stop();
        setBtScanningState(false);
        setStatus(tr("Scan BT interrompu (%1 trouvé(s)).").arg((int)m_btDevices.size()));
        emit toast(tr("Scan interrompu"));
        return;
    }
    setStatus(tr("Autorisation Bluetooth…"));
    platformRequestBluetoothPermissions([this](bool granted) {
        if (!granted) {
            setStatus(tr("Permission Bluetooth refusée."), true);
            emit toast(tr("Permission BT refusée"));
            emit showDialog(tr("Bluetooth"),
                tr("Active « Appareils à proximité » (Android 12+) ou Position "
                   "(Android 11-) pour ECU Drive."),
                tr("OK"));
            return;
        }
        loadBondedBtDevices();
        ensureBtAgent();
        if (!m_btAgent) return;
        if (m_btAgent->isActive()) m_btAgent->stop();
        m_btDevices.clear();
        emit btDevicesChanged();
        setBtScanningState(true);
        emit toast(tr("Scan Bluetooth…"));
        m_btAgent->start(QBluetoothDeviceDiscoveryAgent::ClassicMethod);
    });
#else
    setStatus(tr("Bluetooth non disponible dans ce build."), true);
#endif
}

void DriveController::toggleConnect() {
    if (m_connected) {
        setStatus(tr("Déconnexion…"));
        m_userDisconnect = true;
        m_elm->disconnectPort();
        return;
    }
    emit toast(tr("Connexion…"));
#if defined(ELM_HAVE_BLUETOOTH)
    if (m_btAgent && m_btAgent->isActive()) {
        m_btAgent->stop();
        setBtScanningState(false);
    }
    if (m_selectedBt.isEmpty()) {
        for (const auto& d : m_btDevices) {
            if (d.likelyObd) { m_selectedBt = d.addr; break; }
        }
        if (m_selectedBt.isEmpty() && !m_btDevices.empty())
            m_selectedBt = m_btDevices.front().addr;
        if (!m_selectedBt.isEmpty())
            emit selectedBtChanged();
    }
    if (!m_selectedBt.isEmpty()) {
        platformRequestBluetoothPermissions([this](bool granted) {
            if (!granted) {
                setStatus(tr("Permission Bluetooth refusée."), true);
                emit toast(tr("Permission BT refusée"));
                return;
            }
            QTimer::singleShot(0, this, [this]() { toggleConnectAfterPerms(); });
        });
        return;
    }
#endif
    // USB
    if (m_ports.isEmpty()) {
#if defined(Q_OS_ANDROID)
        setStatus(tr("Aucun périphérique BT sélectionné. USB Android non disponible dans ce build."), true);
        emit toast(tr("Sélectionne un module Bluetooth"));
#else
        setStatus(tr("Aucun port USB détecté. Branche le dongle puis Rafraîchir."), true);
        emit toast(tr("Aucun port USB"));
#endif
        return;
    }
    QString port = m_selectedPort;
    if (port.isEmpty())
        port = m_ports.first().section(QLatin1Char('|'), 1, 1);
    setStatus(tr("Connexion USB… (%1)").arg(port));
    m_elm->connectPort(port, 0);
}

void DriveController::ecuPickerAccepted(const QString& ecuId) {
    if (m_pendingRom.isEmpty()) return;
    if (ecuId.isEmpty()) {
        emit toast(tr("Sélectionne un ECU"));
        emit showEcuPicker(availableEcuIds(), QFileInfo(m_pendingRomPath).fileName());
        return;
    }
    const QByteArray rom = m_pendingRom;
    const QString path = m_pendingRomPath;
    if (applyRomBinary(rom, ecuId, path)) {
        m_pendingRom.clear();
        m_pendingRomPath.clear();
    } else {
        emit showEcuPicker(availableEcuIds(), QFileInfo(path).fileName());
    }
}

void DriveController::ecuPickerCancelled() {
    m_pendingRom.clear();
    m_pendingRomPath.clear();
}

// ── Session ───────────────────────────────────────────────────────────────────

void DriveController::toggleSession() {
    if (m_sessionOn) stopSession();
    else startSession();
}

void DriveController::startSession() {
    if (m_sessionOn) return;
    if (!m_validator.isReady()) {
        setStatus(tr("Importe d'abord un tune / une ROM valide."), true);
        emit toast(tr("Importe un tune d'abord"));
        return;
    }
    if (!m_connected) {
        setStatus(tr("Connecte l'ELM avant de lancer la session."), true);
        emit toast(tr("Connecte le Bluetooth d'abord"));
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
    m_sessionOn = true;
    emit sessionChanged();
    // Respecte l'onglet courant (Capteurs → catalogue enrichi).
    applyPollingForCurrentPage();
    autoStartCsv();
    platformStartLoggingService(tr("ECU Drive"), tr("Session conduite — logging OBD actif"));
    ensureBackgroundWatchdog(true);
#if defined(Q_OS_ANDROID)
    // Demande l'exemption batterie une fois (OEM agressifs sinon coupent le BT).
    if (!platformIsIgnoringBatteryOptimizations()) {
        QTimer::singleShot(600, this, [this]() {
            if (!m_sessionOn) return;
            if (platformRequestIgnoreBatteryOptimizations())
                emit toast(tr("Autorise « Pas d'optimisation » pour le logging écran éteint"));
        });
    }
#endif
    m_verdict = tr("Acquisition…");
    emit driveChanged();
    endBusy();
    setStatus(tr("Session conduite active — %1 PID(s).").arg(qp.size()));
}

void DriveController::stopSession() {
    if (!m_sessionOn) return;
    beginBusy(tr("Arrêt de la session…"));
    m_elm->stopPolling();
    m_sessionOn = false;
    emit sessionChanged();
    ensureBackgroundWatchdog(false);
    platformStopLoggingService();
    autoStopCsv();
    const auto sum = m_session.finish();
    endBusy();
    if (sum.ticks > 0) {
        // Résumé session
        QString hot;
        for (const auto& h : sum.hotspots)
            hot += tr("\n  (%1,%2) ×%3 |Δ|%4")
                       .arg(h.gx).arg(h.gy).arg(h.count).arg(h.meanAbsDelta(), 0, 'f', 1);
        const QString csv = sum.csvPath.isEmpty() ? m_lastCsv : sum.csvPath;
        QString body = tr("Ticks %1 | OK %2 · Warn %3 · Fail %4\nDans tolérance : %5 %\n"
                          "Pic |Δ| %6 sur %7\nHotspots:%8\n\nCSV : %9")
                           .arg(sum.ticks).arg(sum.ok).arg(sum.warn).arg(sum.fail)
                           .arg(sum.okRatio(), 0, 'f', 1)
                           .arg(sum.peakAbsDelta, 0, 'f', 1)
                           .arg(sum.peakMap.isEmpty() ? QStringLiteral("—") : sum.peakMap)
                           .arg(hot.isEmpty() ? tr("\n  (aucun)") : hot)
                           .arg(csv.isEmpty() ? tr("(aucun)") : QFileInfo(csv).fileName());
        if (!m_pendingDisconnectReason.isEmpty()) {
            body += tr("\n\n—\nModule déconnecté : %1").arg(m_pendingDisconnectReason);
            m_pendingDisconnectReason.clear();
        }
        setStatus(tr("Session terminée."));
        emit toast(tr("Session terminée"));
        emit showDialog(tr("Export des logs terminé"), body, tr("OK"));
    } else {
        setStatus(tr("Session arrêtée (aucune donnée)."));
    }
    applyPollingForCurrentPage();
}

// ── OBD / Sensors ─────────────────────────────────────────────────────────────

void DriveController::onPid(quint8 pid, double value, const QString&, const QString& unit) {
    m_live[pid] = value;
    if (!unit.isEmpty()) m_liveUnit[pid] = unit;
    m_knownUnsupported.remove(pid);
    if (m_sessionOn) runValidation();
    refreshTurboLive();
    // Mise à jour capteurs en temps réel
    refreshSensorsTable();
}

bool DriveController::pidLikelySupported(quint8 pid) const {
    if (m_knownUnsupported.contains(pid))
        return false;
    // Tant que le probe n'a pas répondu, on tente le catalogue (sauf blacklist).
    if (!m_supportProbed || m_ecuSupportedPids.isEmpty())
        return true;
    return m_ecuSupportedPids.contains(pid);
}

QList<quint8> DriveController::sensorsPollPids() const {
    QList<quint8> pids;
    for (const auto& p : ecu::obd2::livePids()) {
        if (!pidLikelySupported(p.pid))
            continue;
        pids.append(p.pid);
    }
    // Essentiels turbo / conduite : on les garde même si le bitmap est incomplet.
    for (quint8 p : { quint8{0x0C}, quint8{0x0D}, quint8{0x04}, quint8{0x0B},
                      quint8{0x10}, quint8{0x05}, quint8{0x0F} }) {
        if (m_knownUnsupported.contains(p))
            continue;
        if (!pids.contains(p))
            pids.prepend(p);
    }
    return pids;
}

void DriveController::setUiPage(int page) {
    if (page < 0) page = 0;
    if (page > 2) page = 2;
    if (m_uiPage == page) {
        // Re-entrée onglet Capteurs : relance le poll capteurs.
        if (page == 1)
            ensureSensorsPolling();
        return;
    }
    m_uiPage = page;
    applyPollingForCurrentPage();
}

void DriveController::applyPollingForCurrentPage() {
    if (!m_connected || !m_elm) return;
    if (m_dtcAwaiting > 0 || m_dtcClearPending) {
        refreshTurboLive();
        return;
    }

    if (m_sessionOn) {
        QList<quint8> pids;
        for (auto p : m_validator.requiredPids())
            pids.append(static_cast<quint8>(p));
        if (pids.isEmpty())
            pids = { 0x0C, 0x04, 0x0B, 0x33 };
        // Onglet Capteurs : enrichit avec le catalogue supporté (sinon la plupart
        // des lignes restent à « — » pendant la session).
        if (m_uiPage == 1) {
            for (quint8 p : sensorsPollPids()) {
                if (!pids.contains(p))
                    pids.append(p);
            }
        }
        m_elm->startPolling(pids, m_uiPage == 1 ? 280 : 180);
        refreshTurboLive();
        return;
    }

    if (m_uiPage == 1) {
        ensureSensorsPolling();
        return;
    }
    ensureTurboPolling();
}

void DriveController::ensureSensorsPolling() {
    if (!m_connected || !m_elm) return;
    if (m_dtcAwaiting > 0 || m_dtcClearPending) return;

    if (m_sessionOn) {
        QList<quint8> pids;
        for (auto p : m_validator.requiredPids())
            pids.append(static_cast<quint8>(p));
        if (pids.isEmpty())
            pids = { 0x0C, 0x04, 0x0B, 0x33 };
        for (quint8 p : sensorsPollPids()) {
            if (!pids.contains(p))
                pids.append(p);
        }
        m_elm->startPolling(pids, 280);
        refreshTurboLive();
        return;
    }

    QList<quint8> pids = sensorsPollPids();
    if (pids.isEmpty()) {
        for (const auto& p : ecu::obd2::livePids())
            pids.append(p.pid);
    }
    m_elm->startPolling(pids, 300);
    refreshTurboLive();
}

void DriveController::refreshSensorsTable() {
    const auto& pids = ecu::obd2::livePids();
    QVariantList next;
    next.reserve(pids.size());
    for (const auto& p : pids) {
        // Masque les PID clairement non supportés une fois le probe connu,
        // sauf s'ils ont déjà une valeur live (ex. bitmap incomplet).
        const bool has = m_live.contains(p.pid);
        if (!has && m_supportProbed && !m_ecuSupportedPids.isEmpty()
                && !m_ecuSupportedPids.contains(p.pid))
            continue;
        if (!has && m_knownUnsupported.contains(p.pid))
            continue;

        QVariantMap row;
        row[QStringLiteral("name")] = QString::fromUtf8(p.name);
        row[QStringLiteral("unit")] = QString::fromUtf8(p.unit);
        row[QStringLiteral("value")] = has
            ? QString::number(m_live.value(p.pid), 'f', 1)
            : QStringLiteral("—");
        next.append(row);
    }
    if (next == m_sensorValues)
        return;
    m_sensorValues = std::move(next);
    emit sensorValuesChanged();
}

void DriveController::ensureTurboPolling() {
    if (!m_connected || !m_elm) return;
    if (m_sessionOn || m_dtcAwaiting > 0 || m_dtcClearPending) { refreshTurboLive(); return; }
    QList<quint8> pids = { 0x0B, 0x33, 0x10, 0x0C, 0x04 };
    pids.erase(std::remove_if(pids.begin(), pids.end(),
                              [this](quint8 p) { return m_knownUnsupported.contains(p); }),
               pids.end());
    if (pids.isEmpty())
        pids = { 0x0C, 0x0B, 0x10 };
    m_elm->startPolling(pids, 180);
    refreshTurboLive();
}

void DriveController::ensureBackgroundWatchdog(bool on) {
    if (!on) {
        if (m_bgWatchdog)
            m_bgWatchdog->stop();
        return;
    }
    if (!m_bgWatchdog) {
        m_bgWatchdog = new QTimer(this);
        m_bgWatchdog->setInterval(15000);
        connect(m_bgWatchdog, &QTimer::timeout, this, &DriveController::onBackgroundWatchdog);
    }
    if (!m_bgWatchdog->isActive())
        m_bgWatchdog->start();
}

void DriveController::onBackgroundWatchdog() {
    if (!m_sessionOn) {
        ensureBackgroundWatchdog(false);
        return;
    }
    // Réaffirme FGS + notif (certains OEM tuent le service silencieusement).
    const double rpm = m_live.value(0x0C, 0.0);
    const QString text = m_uiSuspended
        ? tr("BG · RPM %1 · %2 lignes CSV")
              .arg(rpm > 0 ? QString::number(rpm, 'f', 0) : QStringLiteral("—"))
              .arg(m_csvRows)
        : tr("Session · RPM %1 · %2 lignes")
              .arg(rpm > 0 ? QString::number(rpm, 'f', 0) : QStringLiteral("—"))
              .arg(m_csvRows);
    if (m_uiSuspended)
        platformStartLoggingService(tr("ECU Drive"), text);
    else
        platformUpdateLoggingService(tr("ECU Drive"), text);
    if (m_csv && m_csv->isOpen())
        m_csv->flush();
}

void DriveController::refreshTurboLive() {
    auto fmt = [](bool ok, const QString& t) { return ok ? t : QStringLiteral("—"); };
    const bool hasMap  = m_live.contains(0x0B);
    const bool hasBaro = m_live.contains(0x33);
    const bool hasMaf  = m_live.contains(0x10);
    const bool hasRpm  = m_live.contains(0x0C);
    const double mapMbar  = hasMap  ? ecu::TuneValidator::mapAbsKpaToMbar(m_live.value(0x0B)) : 0.0;
    const double baroMbar = hasBaro ? ecu::TuneValidator::mapAbsKpaToMbar(m_live.value(0x33)) : 0.0;
    m_turboMap   = fmt(hasMap,  tr("%1 mbar").arg(mapMbar,  0, 'f', 0));
    m_turboBaro  = fmt(hasBaro, tr("%1 mbar").arg(baroMbar, 0, 'f', 0));
    if (hasMap && hasBaro)
        m_turboDelta = tr("%1 mbar").arg(mapMbar - baroMbar, 0, 'f', 0);
    else
        m_turboDelta = QStringLiteral("—");
    m_turboMaf = fmt(hasMaf, tr("%1 g/s").arg(m_live.value(0x10), 0, 'f', 1));
    m_turboRpm = fmt(hasRpm, tr("%1 tr/min").arg(m_live.value(0x0C), 0, 'f', 0));
    emit turboChanged();
}

// ── Validation / Drive UI ─────────────────────────────────────────────────────

void DriveController::runValidation() {
    const auto results = m_validator.evaluateAll(snapshot());
    if (m_session.active()) m_session.ingest(results);
    appendCsv(results);
    if (m_uiSuspended) {
        if (const auto boost = primaryBoost(results)) {
            const auto shown = m_hyst.update(boost->status);
            if (shown == ecu::ValidationStatus::Fail) maybeAlert();
        }
        return;
    }
    updateDriveUi(results);
    refreshMapsList(results);
    const auto& c = m_session.current();
    m_sessionLive = tr("OK %1 · Warn %2 · Fail %3 (%4 %)")
                        .arg(c.ok).arg(c.warn).arg(c.fail).arg(c.okRatio(), 0, 'f', 0);
    emit driveChanged();
}

void DriveController::updateDriveUi(const std::vector<ecu::ValidationResult>& results) {
    const double rpm  = m_live.value(0x0C, 0.0);
    const double load = m_live.value(0x04, 0.0);
    m_rpmLoad = tr("RPM %1  ·  Charge %2 %")
                    .arg(rpm  > 0 ? QString::number(rpm,  'f', 0) : QStringLiteral("—"))
                    .arg(load > 0 ? QString::number(load, 'f', 0) : QStringLiteral("—"));

    const auto boost = primaryBoost(results);
    if (!boost || boost->status == ecu::ValidationStatus::NoData) {
        m_verdict  = rpm > 400 ? tr("En attente…") : tr("Ralenti — accélère");
        m_boostBig = QStringLiteral("—");
        m_boostSub = {};
        emit driveChanged();
        return;
    }
    const double meas = m_emaMeas.push(boost->measured);
    const double exp  = m_emaExp.push(boost->expected);
    const double d    = meas - exp;
    const auto shown  = m_hyst.update(boost->status);
    const QString unit = boost->unit.isEmpty() ? QStringLiteral("mbar") : boost->unit;
    m_boostBig = tr("%1 / %2 %3").arg(meas, 0, 'f', 0).arg(exp, 0, 'f', 0).arg(unit);
    m_boostSub = tr("Δ %1 %2").arg(d, 0, 'f', 0).arg(unit);
    switch (shown) {
        case ecu::ValidationStatus::Ok:
            m_verdict = tr("TURBO OK"); break;
        case ecu::ValidationStatus::Warn:
            m_verdict = d < 0 ? tr("LÉGER UNDERBOOST") : tr("LÉGER OVERBOOST"); break;
        case ecu::ValidationStatus::Fail:
            m_verdict = d < 0 ? tr("UNDERBOOST") : tr("OVERBOOST"); maybeAlert(); break;
        default:
            m_verdict = tr("—"); break;
    }
    emit driveChanged();
}

void DriveController::refreshMapsList(const std::vector<ecu::ValidationResult>& results) {
    m_mapsList.clear();
    std::vector<ecu::ValidationResult> sorted = results;
    std::sort(sorted.begin(), sorted.end(),
              [](const ecu::ValidationResult& a, const ecu::ValidationResult& b) {
                  const double da = a.status == ecu::ValidationStatus::NoData ? -1.0 : std::abs(a.delta);
                  const double db = b.status == ecu::ValidationStatus::NoData ? -1.0 : std::abs(b.delta);
                  return da > db;
              });
    int shown = 0;
    for (const auto& r : sorted) {
        if (r.status == ecu::ValidationStatus::NoData) continue;
        QVariantMap m;
        m[QStringLiteral("status")] = statusLabel(r.status);
        m[QStringLiteral("mapName")] = r.mapName;
        m[QStringLiteral("delta")] = QString::number(r.delta, 'f', 1);
        m[QStringLiteral("unit")] = r.unit;
        m_mapsList.append(m);
        if (++shown >= 8) break;
    }
    emit driveChanged();
}

std::optional<ecu::ValidationResult> DriveController::primaryBoost(
    const std::vector<ecu::ValidationResult>& results) const {
    for (const auto& r : results)
        if (r.category == QStringLiteral("boost") && r.status != ecu::ValidationStatus::NoData)
            return r;
    for (const auto& r : results)
        if (r.status != ecu::ValidationStatus::NoData) return r;
    return std::nullopt;
}

void DriveController::maybeAlert() {
    const int streak = m_hyst.failStreak();
    if (streak < 3 || streak == m_lastAlertAt || streak % 3 != 0) return;
    m_lastAlertAt = streak;
    if (m_beepAlert) platformAlertBeep();
}

ecu::LivePidSnapshot DriveController::snapshot() const {
    ecu::LivePidSnapshot s;
    for (auto it = m_live.cbegin(); it != m_live.cend(); ++it)
        s[static_cast<std::uint8_t>(it.key())] = it.value();
    return s;
}

// ── DTC ───────────────────────────────────────────────────────────────────────

void DriveController::readDtcs() {
    if (!m_connected || !m_elm) { emit toast(tr("Connecte l'ELM d'abord")); return; }
    if (m_dtcAwaiting > 0 || m_dtcClearPending) return;
    m_elm->stopPolling();
    m_dtcFlags.clear();
    refreshDtcList();
    m_dtcAwaiting = 2;
    emit connectedChanged();
    setStatus(tr("Lecture DTC modes 03 + 07…"));
    emit toast(tr("Lecture DTC…"));
    m_elm->readDtcs(false);
    m_elm->readDtcs(true);
}

void DriveController::clearDtcs() {
    if (!m_connected || !m_elm) { emit toast(tr("Connecte l'ELM d'abord")); return; }
    if (m_dtcAwaiting > 0 || m_dtcClearPending) return;
    m_elm->stopPolling();
    m_dtcClearPending = true;
    setStatus(tr("Effacement DTC (mode 04)…"));
    emit toast(tr("Effacement DTC…"));
    m_elm->clearDtcs();
}

QString DriveController::copyDtcs() const {
    QStringList lines;
    QStringList keys = m_dtcFlags.keys(); keys.sort();
    for (const QString& code : keys) {
        const int f = m_dtcFlags.value(code);
        const QString fam = [&]() -> QString {
            if (code.isEmpty()) return {};
            switch (code[0].toLatin1()) {
                case 'P': return tr("Powertrain");
                case 'C': return tr("Chassis");
                case 'B': return tr("Body");
                case 'U': return tr("Network");
                default:  return QStringLiteral("?");
            }
        }();
        const QString st = (f & 2) ? tr("en cours") : tr("mémorisé");
        lines << QStringLiteral("%1\t%2\t%3").arg(code, fam, st);
    }
    const QString text = lines.join(QLatin1Char('\n'));
    QApplication::clipboard()->setText(text);
    return text;
}

void DriveController::mergeDtcCodes(const QStringList& codes, bool pending) {
    for (const QString& c : codes) {
        if (c.isEmpty()) continue;
        int flags = m_dtcFlags.value(c, 0);
        flags |= pending ? 2 : 1;
        m_dtcFlags[c] = flags;
    }
    refreshDtcList();
}

void DriveController::refreshDtcList() {
    m_dtcList.clear();
    QStringList keys = m_dtcFlags.keys(); keys.sort();
    for (const QString& code : keys) {
        const int f = m_dtcFlags.value(code);
        QVariantMap m;
        m[QStringLiteral("code")] = code;
        m[QStringLiteral("status")] = (f & 2) ? tr("en cours") : tr("mémorisé");
        m[QStringLiteral("family")] = code.isEmpty() ? QString() : [&]() -> QString {
            switch (code[0].toLatin1()) {
                case 'P': return tr("Powertrain");
                case 'C': return tr("Chassis");
                case 'B': return tr("Body");
                case 'U': return tr("Network");
                default:  return QStringLiteral("?");
            }
        }();
        m_dtcList.append(m);
    }
    emit dtcListChanged();
}

// ── Console OBD brute ─────────────────────────────────────────────────────────

void DriveController::sendRawCommand(const QString& cmd) {
    if (!m_elm || !m_connected) {
        appendRawLog(tr("[Diag] ELM non connecté"));
        return;
    }
    appendRawLog(QStringLiteral("> %1").arg(cmd));
    m_elm->sendRawCommand(cmd);
}

void DriveController::onRawResponse(const QString& command, const QString& response) {
    appendRawLog(QStringLiteral("< %1 → %2").arg(command, response));
}

void DriveController::appendRawLog(const QString& line) {
    const QStringList lines = m_rawLog.split(QLatin1Char('\n'));
    QStringList kept = lines;
    if (kept.size() > 200) kept = kept.mid(kept.size() - 200);
    kept.append(line);
    m_rawLog = kept.join(QLatin1Char('\n'));
    emit rawLogChanged();
}

// ── Security Access ───────────────────────────────────────────────────────────

void DriveController::computeSaKey(const QString& proto, const QString& seedHex, const QString& ecuKeyHex) {
    const QString sh = QString(seedHex).remove(QStringLiteral("0x"));
    const QString kh = QString(ecuKeyHex).remove(QStringLiteral("0x"));
    bool seedOk = false, keyOk = false;
    const quint32 seed = sh.toUInt(&seedOk, 16);
    const quint16 ecuKey = kh.isEmpty() ? 0 : kh.toUShort(&keyOk, 16);
    if (!seedOk || sh.isEmpty()) {
        m_saResult = tr("Seed invalide");
        emit saResultChanged();
        return;
    }
    const auto algoOpt = ecu::SecurityAccess::fromName(proto.toStdString());
    if (!algoOpt) {
        m_saResult = tr("Algorithme inconnu : %1").arg(proto);
        emit saResultChanged();
        return;
    }
    const auto keyOpt = ecu::SecurityAccess::compute(*algoOpt, seed, ecuKey);
    if (!keyOpt) {
        m_saResult = tr("Calcul échoué");
        emit saResultChanged();
        return;
    }
    m_saResult = QString::number(*keyOpt, 16).toUpper();
    emit saResultChanged();
}

void DriveController::sendSecurityAccess(const QString& proto, int level, const QString& ecuKeyHex, bool kwp) {
    if (!m_elm || !m_connected) {
        appendRawLog(tr("[SA] ELM non connecté"));
        return;
    }
    const QString kh = QString(ecuKeyHex).remove(QStringLiteral("0x"));
    bool keyOk = false;
    const quint16 ecuKey = kh.isEmpty() ? 0 : kh.toUShort(&keyOk, 16);
    appendRawLog(tr("[SA] Requête seed %1 niveau %2…").arg(proto).arg(level));
    m_elm->sendSecurityAccessRequest(proto, level, ecuKey, kwp);
}

void DriveController::sendActuatorOn(const QString& cmd) {
    if (!m_elm || !m_connected) { appendRawLog(tr("[Act] Non connecté")); return; }
    appendRawLog(QStringLiteral("> %1").arg(cmd));
    m_elm->sendRawCommand(cmd);
}

void DriveController::sendActuatorOff(const QString& cmd) {
    if (!m_elm || !m_connected) { appendRawLog(tr("[Act] Non connecté")); return; }
    appendRawLog(QStringLiteral("> %1").arg(cmd));
    m_elm->sendRawCommand(cmd);
}

// ── Tune / ROM ────────────────────────────────────────────────────────────────

void DriveController::importTune() {
    // File picker géré côté C++ pour éviter les dépendances QML runtime.
    const QString path = QFileDialog::getOpenFileName(
        nullptr, tr("Importer tune / ROM"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("Tune / ROM (*.ecutune *.bin *.zip);;Tous les fichiers (*.*)"));
    if (!path.isEmpty())
        QTimer::singleShot(0, this, [this, path]() { loadTuneFile(path); });
}

void DriveController::loadTuneFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(tr("Impossible d'ouvrir : %1").arg(path), true);
        emit toast(tr("Fichier inaccessible"));
        return;
    }
    const QByteArray data = f.readAll();
    f.close();

    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QStringLiteral("ecutune") || ext == QStringLiteral("zip")) {
        auto result = ecu::TunePackageIo::readZip(data);
        if (!result) {
            setStatus(tr("Fichier .ecutune invalide : %1").arg(result.error()), true);
            emit toast(tr("Tune invalide"));
            return;
        }
        applyTunePackage(*result, path);
    } else {
        // ROM binaire
        m_pendingRom     = data;
        m_pendingRomPath = path;
        const QStringList ids = availableEcuIds();
        if (!m_autoEcuId.isEmpty()) {
            const QString ecu = m_autoEcuId;
            m_autoEcuId.clear();
            m_pendingRom = data;
            m_pendingRomPath = path;
            if (!applyRomBinary(data, ecu, path)) {
                m_pendingRom = data;
                m_pendingRomPath = path;
                emit showEcuPicker(ids, QFileInfo(path).fileName());
            } else {
                m_pendingRom.clear();
                m_pendingRomPath.clear();
            }
        } else if (m_suppressEcuPromptOnce && !ids.isEmpty()) {
            m_suppressEcuPromptOnce = false;
            const QString lastEcu = QSettings().value(QStringLiteral("drive/lastEcu")).toString();
            const QString ecu = ids.contains(lastEcu) ? lastEcu : ids.first();
            m_pendingRom = data;
            m_pendingRomPath = path;
            if (!applyRomBinary(data, ecu, path)) {
                m_pendingRom = data;
                m_pendingRomPath = path;
                emit showEcuPicker(ids, QFileInfo(path).fileName());
            } else {
                m_pendingRom.clear();
                m_pendingRomPath.clear();
            }
        } else {
            m_suppressEcuPromptOnce = false;
            if (ids.isEmpty()) {
                setStatus(tr("Aucune recette ECU embarquée — importe un fichier .ecutune."), true);
                emit toast(tr("Aucun ECU disponible"));
                return;
            }
            m_pendingRom = data;
            m_pendingRomPath = path;
            emit showEcuPicker(ids, QFileInfo(path).fileName());
        }
    }
}

void DriveController::applyTunePackage(const ecu::TunePackage& pkg, const QString& path) {
    beginBusy(tr("Chargement du tune…"));
    const bool ok = m_validator.loadTunePackage(pkg);
    endBusy();
    if (!ok) {
        setStatus(tr("Tune invalide"), true);
        emit toast(tr("Tune invalide"));
        return;
    }
    m_tunePath  = path;
    m_tuneReady = true;
    const int mapCount = static_cast<int>(m_validator.rules().size());
    m_tuneLabel = tr("ROM : %1\nECU %2 · MD5 %3 · %4 map(s) · prêt=%5")
                      .arg(QFileInfo(path).fileName(),
                           m_validator.ecuId(),
                           m_validator.romMd5().left(8),
                           QString::number(mapCount),
                           tr("oui"));
    QSettings().setValue(QStringLiteral("drive/lastTune"), path);
    QSettings().setValue(QStringLiteral("drive/lastEcu"), m_validator.ecuId());
    emit tuneLabelChanged();
    setStatus(tr("Tune chargé — %1 map(s)").arg(mapCount));
    emit toast(tr("Tune prêt"));
}

bool DriveController::applyRomBinary(const QByteArray& rom, const QString& ecuId, const QString& path) {
    beginBusy(tr("Analyse ROM…"), 0);
    const bool ok = m_validator.loadRom(rom, ecuId);
    endBusy();
    if (!ok) {
        setStatus(tr("ROM incompatible avec ECU %1 — choisis une autre recette.").arg(ecuId), true);
        emit toast(tr("ROM incompatible"));
        return false;
    }
    m_tunePath  = path;
    m_tuneReady = true;
    const int mapCount = static_cast<int>(m_validator.rules().size());
    m_tuneLabel = tr("ROM : %1\nECU %2 · MD5 %3 · %4 map(s) · prêt=%5")
                      .arg(QFileInfo(path).fileName(),
                           m_validator.ecuId(),
                           m_validator.romMd5().left(8),
                           QString::number(mapCount),
                           tr("oui"));
    QSettings().setValue(QStringLiteral("drive/lastTune"), path);
    QSettings().setValue(QStringLiteral("drive/lastEcu"), ecuId);
    emit tuneLabelChanged();
    setStatus(tr("ROM chargée (%1 maps) — connecte l'ELM puis lance la session.").arg(mapCount));
    emit toast(tr("ROM prête"));
    return true;
}

QStringList DriveController::availableEcuIds() {
    if (!m_ecuIdsCache.isEmpty()) return m_ecuIdsCache;

    QSet<QString> ids;

    auto addIfRecipe = [&](const QString& ecuId) {
        if (ecuId.isEmpty() || ids.contains(ecuId)) return;
        if (QFile::exists(QStringLiteral(":/ressources/%1/open_damos.json").arg(ecuId))
            || QFile::exists(QDir(ecu::OpenDamos::userRecipeDir()).filePath(ecuId + QStringLiteral("/open_damos.json"))))
            ids.insert(ecuId);
    };

    // QRC : certains builds (Android) n'exposent pas les dossiers via QDir::Dirs.
    const QDir qrcDir(QStringLiteral(":/ressources"));
    for (const QString& sub : qrcDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
        addIfRecipe(sub);

    const QDir userDir(ecu::OpenDamos::userRecipeDir());
    if (userDir.exists()) {
        for (const QString& sub : userDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            addIfRecipe(sub);
    }

    m_ecuIdsCache = ids.values();
    m_ecuIdsCache.sort();
    return m_ecuIdsCache;
}

// ── CSV ───────────────────────────────────────────────────────────────────────

QString DriveController::sessionLogScratchDir() const {
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return (cache.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            : cache) + QStringLiteral("/datalog");
}

void DriveController::autoStartCsv() {
    if (m_csv) return;
    const QString dir = sessionLogScratchDir();
    if (!QDir().mkpath(dir)) {
        setStatus(tr("Dossier logs inaccessible"), true);
        return;
    }
    m_lastCsv = dir + QStringLiteral("/drive_%1.csv")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    m_csv = new QFile(m_lastCsv, this);
    if (!m_csv->open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_csv->deleteLater(); m_csv = nullptr;
        setStatus(tr("Écriture CSV impossible"), true);
        return;
    }
    m_session.setCsvPath(m_lastCsv);
    m_csvMaps.clear();
    m_lastCsvWriteMs = 0;
    m_csvRows = 0;
    QTextStream ts(m_csv);
    ts << QStringLiteral("# ecu-drive-session v2\n");
    ts << QStringLiteral("# app=") << QStringLiteral(APP_VERSION)
       << QStringLiteral(" ecu=") << m_validator.ecuId()
       << QStringLiteral(" started=")
       << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
    QString header = QStringLiteral("time,rpm,load_pct,speed_kmh,map_mbar,maf_gs");
    for (const auto& rule : m_validator.rules()) {
        if (!rule.enabled) continue;
        const QString col = csvSanitizeMapName(QString::fromStdString(rule.mapName));
        m_csvMaps.append(QString::fromStdString(rule.mapName));
        header += QStringLiteral(",%1_meas,%1_exp,%1_delta,%1_status,%1_unit").arg(col);
    }
    ts << header << '\n';
    m_csv->flush();
    setStatus(tr("Log CSV en cours"));
}

void DriveController::autoStopCsv() {
    if (!m_csv) return;
    { QTextStream ts(m_csv); ts << QStringLiteral("# end\n"); }
    m_csv->flush(); m_csv->close();
    m_csv->deleteLater(); m_csv = nullptr;
    m_csvMaps.clear();
}

QString DriveController::csvSanitizeMapName(const QString& n) {
    QString s = n;
    for (QChar& ch : s)
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('_'))
            ch = QLatin1Char('_');
    return s;
}

void DriveController::appendCsv(const std::vector<ecu::ValidationResult>& results) {
    if (!m_csv) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastCsvWriteMs > 0 && (now - m_lastCsvWriteMs) < 250) return;
    const double rpm  = m_live.value(0x0C, 0.0);
    if (rpm < 200.0) return;
    m_lastCsvWriteMs = now;
    QHash<QString, const ecu::ValidationResult*> byName;
    for (const auto& r : results) byName.insert(r.mapName, &r);
    QTextStream ts(m_csv);
    ts << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
       << ',' << QString::number(rpm, 'f', 0)
       << ',' << QString::number(m_live.value(0x04, 0.0), 'f', 1)
       << ',' << QString::number(m_live.value(0x0D, 0.0), 'f', 0)
       << ',' << QString::number(ecu::TuneValidator::mapAbsKpaToMbar(m_live.value(0x0B, 0.0)), 'f', 0)
       << ',' << QString::number(m_live.value(0x10, 0.0), 'f', 2);
    for (const QString& mn : m_csvMaps) {
        const ecu::ValidationResult* r = byName.value(mn, nullptr);
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
    ++m_csvRows;
}

QString DriveController::promptSaveLogAs(const QString& sourceCsv) {
    if (sourceCsv.isEmpty() || !QFile::exists(sourceCsv)) return {};
    const QString name = QFileInfo(sourceCsv).fileName();
#if defined(Q_OS_ANDROID)
    const QString media = platformSaveToDownloads(sourceCsv, name);
    if (!media.isEmpty()) { m_lastCsv = media; return media; }
#endif
    return {};
}

void DriveController::shareLastLog() {
    if (m_lastCsv.isEmpty() || !QFile::exists(m_lastCsv)) {
        emit showDialog(tr("Logs"), tr("Aucun CSV de session récente."), tr("OK"));
        return;
    }
    if (!platformShareFile(m_lastCsv, QStringLiteral("text/csv")))
        emit showDialog(tr("Partage"), tr("Partage système indisponible."), tr("OK"));
}

// ── Mise à jour ───────────────────────────────────────────────────────────────

void DriveController::checkUpdates() {
    setStatus(tr("Vérification des mises à jour…"));
    m_updater->check();
}

// ── Bluetooth (implémentation) ────────────────────────────────────────────────

#if defined(ELM_HAVE_BLUETOOTH)
void DriveController::ensureBtAgent() {
    if (m_btAgent) return;
    m_btAgent = new QBluetoothDeviceDiscoveryAgent(this);
    m_btAgent->setLowEnergyDiscoveryTimeout(0);

    connect(m_btAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, [this](const QBluetoothDeviceInfo& info) {
        if (info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration)
            return;
        addBtDevice(info.address().toString(),
                    info.name().isEmpty() ? info.address().toString() : info.name());
    });

    connect(m_btAgent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, [this]() {
        setBtScanningState(false);
        setStatus(tr("Scan BT terminé — %1 appareil(s).").arg((int)m_btDevices.size()));
        emit toast(tr("Scan terminé"));
    });

    connect(m_btAgent, QOverload<QBluetoothDeviceDiscoveryAgent::Error>::of(
                &QBluetoothDeviceDiscoveryAgent::errorOccurred),
            this, [this]() {
        setBtScanningState(false);
        setStatus(tr("Erreur scan BT : %1").arg(m_btAgent->errorString()), true);
    });
}

void DriveController::setBtScanningState(bool on) {
    m_btScanning = on;
    emit btScanningChanged();
}

void DriveController::selectLastBtDevice() {
    const QString last = QSettings().value(QStringLiteral("drive/lastBt")).toString();
    if (!last.isEmpty()) m_selectedBt = last;
    emit selectedBtChanged();
}

void DriveController::addBtDevice(const QString& addr, const QString& name) {
    if (addr.isEmpty()) return;
    const bool likely = likelyElmBtName(name);
    if (m_btObdOnly && !likely && addr.compare(m_selectedBt, Qt::CaseInsensitive) != 0)
        return;
    for (auto& d : m_btDevices) {
        if (d.addr.compare(addr, Qt::CaseInsensitive) == 0) {
            if (!name.isEmpty()) d.name = name;
            d.likelyObd = likely || d.likelyObd;
            emit btDevicesChanged();
            return;
        }
    }
    m_btDevices.push_back({addr, name.isEmpty() ? addr : name, likely});
    emit btDevicesChanged();
}

void DriveController::loadBondedBtDevices() {
#if defined(Q_OS_ANDROID)
    const QJniObject jarr = QJniObject::callStaticObjectMethod(
        "org/poisson48/ecudrive/BtRfcommHelper", "listBonded", "()[Ljava/lang/String;");
    if (!jarr.isValid()) return;
    QJniEnvironment env;
    jobjectArray arr = jarr.object<jobjectArray>();
    if (!arr) return;
    const jsize n = env->GetArrayLength(arr);
    for (jsize i = 0; i < n; ++i) {
        QJniObject s(env->GetObjectArrayElement(arr, i));
        if (!s.isValid()) continue;
        const QString line = s.toString();
        const QString addr = line.section(QLatin1Char('|'), 0, 0).trimmed();
        const QString name = line.section(QLatin1Char('|'), 1).trimmed();
        addBtDevice(addr, name.isEmpty() ? addr : name);
    }
    if (m_selectedBt.isEmpty()) {
        for (const auto& d : m_btDevices) {
            if (d.likelyObd) { setSelectedBt(d.addr); break; }
        }
    }
#endif
}

void DriveController::toggleConnectAfterPerms() {
    if (m_connected) return;
    if (m_btAgent && m_btAgent->isActive()) {
        m_btAgent->stop();
        setBtScanningState(false);
    }
#if defined(Q_OS_ANDROID)
    QJniObject::callStaticMethod<void>(
        "org/poisson48/ecudrive/BtRfcommHelper", "cancelDiscovery", "()V");
#endif
    if (m_selectedBt.isEmpty()) {
        loadBondedBtDevices();
        if (m_selectedBt.isEmpty()) {
            setStatus(tr("Aucun périphérique BT sélectionné."), true);
            emit toast(tr("Sélectionne un module Bluetooth"));
            return;
        }
    }
#if defined(Q_OS_ANDROID) || defined(Q_OS_LINUX)
    QBluetoothLocalDevice local;
    if (local.isValid()) {
        const auto paired = local.pairingStatus(QBluetoothAddress(m_selectedBt));
        if (paired == QBluetoothLocalDevice::Unpaired) {
            setStatus(tr("Module non appairé — appairer d'abord dans les réglages Bluetooth."), true);
            emit toast(tr("Appaire d'abord le module BT"));
            return;
        }
    }
#endif
    setStatus(tr("Connexion Bluetooth…"));
    emit toast(tr("Connexion BT…"));
    m_elm->connectBluetooth(m_selectedBt);
    QTimer::singleShot(8000, this, [this]() {
        if (!m_connected) emit connectedChanged();
    });
}

bool DriveController::likelyElmBtName(const QString& name) {
    const QString n = name.toLower();
    return n.contains(QStringLiteral("elm")) || n.contains(QStringLiteral("obd"))
        || n.contains(QStringLiteral("vlink")) || n.contains(QStringLiteral("obdii"))
        || n.contains(QStringLiteral("obdlink")) || n.contains(QStringLiteral("scan"));
}
#endif

} // namespace ecu_drive
