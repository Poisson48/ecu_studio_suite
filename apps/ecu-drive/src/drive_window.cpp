#include "drive_window.h"

#include "elm/Elm327.hpp"
#include "ecu/TunePackage.hpp"
#include "updater.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QFrame>
#include <QStackedWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QTimer>
#include <QProgressBar>
#include <QApplication>

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
        m_connectBtn->setText(tr("Déconnecter"));
        setStatus(tr("Connecté — %1").arg(v));
        m_sessionBtn->setEnabled(m_validator.isReady());
#if defined(ELM_HAVE_BLUETOOTH)
        if (m_btCombo && !m_btCombo->currentData().toString().isEmpty()
            && m_elm->isBluetoothTransport()) {
            QSettings().setValue(QStringLiteral("drive/lastBt"),
                                 m_btCombo->currentData().toString());
        }
#endif
    });
    connect(m_elm, &elm::Elm327::disconnected, this, [this]() {
        m_connected = false;
        if (m_sessionOn) stopSession();
        m_connectBtn->setText(tr("Connecter"));
        m_sessionBtn->setEnabled(false);
        setStatus(tr("Déconnecté."));
    });
    connect(m_elm, &elm::Elm327::errorOccurred, this, [this](const QString& e) {
        m_connected = false;
        m_connectBtn->setText(tr("Connecter"));
        setStatus(e, true);
    });
    connect(m_elm, &elm::Elm327::status, this, [this](const QString& s) { setStatus(s); });
    connect(m_elm, &elm::Elm327::pidResult, this, &DriveWindow::onPid);

    connect(m_updater, &Updater::stateChanged, this, &DriveWindow::onUpdaterState);
    connect(m_updater, &Updater::progressChanged, this, &DriveWindow::onUpdaterState);
    connect(m_updater, &Updater::changelogChanged, this, [this]() {
        if (m_updater->hasWhatsNew() && !m_updater->updateAvailable()) {
            QMessageBox::information(this, tr("Quoi de neuf"), m_updater->whatsNewNotes());
            m_updater->acknowledgeNotes();
        }
    });

    // Différer ports / dernier tune : éviter de bloquer le premier show().
    QTimer::singleShot(0, this, [this]() {
        QSettings s;
        const QString last = s.value(QStringLiteral("drive/lastTune")).toString();
        if (!last.isEmpty() && QFile::exists(last)) {
            if (auto pkg = ecu::TunePackageIo::readZipFile(last))
                applyTunePackage(*pkg, last);
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
        for (int i = 0; i < m_btCombo->count(); ++i)
            if (m_btCombo->itemData(i).toString() == addr) return;
        const QString name = info.name();
        const bool star = likelyElmBtName(name);
        const QString label = (star ? QStringLiteral("★ ") : QString())
            + (name.isEmpty() ? addr : QStringLiteral("%1 (%2)").arg(name, addr));
        // Prioriser les ELM en tête de liste
        if (star)
            m_btCombo->insertItem(0, label, addr);
        else
            m_btCombo->addItem(label, addr);
    });
    connect(m_btAgent, &QBluetoothDeviceDiscoveryAgent::finished, this, [this]() {
        m_scanBtBtn->setEnabled(true);
        selectLastBtDevice();
        setStatus(tr("Scan BT classique terminé (%1 appareil(s)).\n"
                     "Module bleu : appairé (PIN 1234/0000) ?")
                      .arg(m_btCombo->count()));
    });
    connect(m_btAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
            [this](QBluetoothDeviceDiscoveryAgent::Error) {
        m_scanBtBtn->setEnabled(true);
        setStatus(tr("Échec scan Bluetooth (adaptateur / permissions)."), true);
    });
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
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    // Bannière MAJ (style ColoCourse)
    m_updateBanner = new QFrame(central);
    m_updateBanner->setVisible(false);
    m_updateBanner->setStyleSheet(QStringLiteral(
        "QFrame { background:#1e3a5f; border:1px solid #3b82f6; border-radius:10px; }"));
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
    root->addWidget(m_updateBanner);

    m_tuneLabel = new QLabel(tr("Aucun tune — importe un fichier .ecutune"), central);
    m_tuneLabel->setWordWrap(true);
    m_tuneLabel->setStyleSheet(QStringLiteral("color:#60a5fa; font-size:13px;"));
    root->addWidget(m_tuneLabel);

    auto* importBtn = new QPushButton(tr("Importer .ecutune…"), central);
    importBtn->setMinimumHeight(44);
    importBtn->setObjectName("accentBtn");
    connect(importBtn, &QPushButton::clicked, this, &DriveWindow::importTune);
    root->addWidget(importBtn);

    // Connexion
    auto* connRow = new QHBoxLayout;
    m_portCombo = new QComboBox(central);
    m_portCombo->setMinimumHeight(36);
    auto* refresh = new QPushButton(tr("↻"), central);
    connect(refresh, &QPushButton::clicked, this, &DriveWindow::refreshPorts);
    connRow->addWidget(m_portCombo, 1);
    connRow->addWidget(refresh);
    root->addLayout(connRow);

#if defined(ELM_HAVE_BLUETOOTH)
    auto* btRow = new QHBoxLayout;
    m_btCombo = new QComboBox(central);
    m_btCombo->setMinimumHeight(36);
    m_btCombo->setPlaceholderText(tr("Bluetooth ELM327…"));
    m_scanBtBtn = new QPushButton(tr("Scan BT"), central);
    connect(m_scanBtBtn, &QPushButton::clicked, this, &DriveWindow::startBtScan);
    btRow->addWidget(m_btCombo, 1);
    btRow->addWidget(m_scanBtBtn);
    root->addLayout(btRow);
#else
    m_btCombo = nullptr;
    m_scanBtBtn = nullptr;
    auto* noBt = new QLabel(tr("Bluetooth non compilé — USB seulement."), central);
    noBt->setStyleSheet(QStringLiteral("color:#f59e0b;"));
    root->addWidget(noBt);
#endif

    m_connectBtn = new QPushButton(tr("Connecter"), central);
    m_connectBtn->setMinimumHeight(44);
    connect(m_connectBtn, &QPushButton::clicked, this, &DriveWindow::toggleConnect);
    root->addWidget(m_connectBtn);

    m_beepChk = new QCheckBox(tr("Bip d'alerte underboost"), central);
    m_beepChk->setChecked(true);
    root->addWidget(m_beepChk);

    // Drive panel
    m_banner = new QFrame(central);
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

    m_boostBig = new QLabel(tr("— / — mbar"), central);
    QFont bf = m_boostBig->font();
    bf.setPointSizeF(26); bf.setBold(true);
    m_boostBig->setFont(bf);
    m_boostBig->setAlignment(Qt::AlignCenter);
    m_boostBig->setStyleSheet(QStringLiteral("color:#60a5fa;"));
    root->addWidget(m_boostBig);

    m_boostSub = new QLabel(tr("Δ —"), central);
    m_boostSub->setAlignment(Qt::AlignCenter);
    m_boostSub->setStyleSheet(QStringLiteral("color:#9ca3af; font-size:16px;"));
    root->addWidget(m_boostSub);

    m_rpmLoad = new QLabel(tr("RPM —  ·  Charge — %"), central);
    m_rpmLoad->setAlignment(Qt::AlignCenter);
    m_rpmLoad->setStyleSheet(QStringLiteral("color:#7c8fa6;"));
    root->addWidget(m_rpmLoad);

    m_sessionLive = new QLabel(tr("Session : —"), central);
    m_sessionLive->setAlignment(Qt::AlignCenter);
    m_sessionLive->setStyleSheet(QStringLiteral("color:#64748b; font-size:12px;"));
    root->addWidget(m_sessionLive);

    m_csvLabel = new QLabel(tr("CSV : inactif"), central);
    m_csvLabel->setAlignment(Qt::AlignCenter);
    m_csvLabel->setStyleSheet(QStringLiteral("color:#64748b; font-size:11px;"));
    root->addWidget(m_csvLabel);

    m_sessionBtn = new QPushButton(tr("▶  Lancer session conduite"), central);
    m_sessionBtn->setObjectName("accentBtn");
    m_sessionBtn->setMinimumHeight(56);
    QFont sf = m_sessionBtn->font();
    sf.setPointSizeF(sf.pointSizeF() + 3); sf.setBold(true);
    m_sessionBtn->setFont(sf);
    m_sessionBtn->setEnabled(false);
    connect(m_sessionBtn, &QPushButton::clicked, this, &DriveWindow::toggleSession);
    root->addWidget(m_sessionBtn);

    auto* shareBtn = new QPushButton(tr("Partager dernier log…"), central);
    connect(shareBtn, &QPushButton::clicked, this, &DriveWindow::shareLastLog);
    root->addWidget(shareBtn);

    auto* updBtn = new QPushButton(tr("Vérifier les mises à jour"), central);
    connect(updBtn, &QPushButton::clicked, this, &DriveWindow::checkUpdatesManual);
    root->addWidget(updBtn);

    root->addStretch();

    m_statusLabel = new QLabel(tr("100 % local — aucune télémétrie."), central);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#7c8fa6; font-size:12px;"));
    root->addWidget(m_statusLabel);
}

void DriveWindow::setStatus(const QString& msg, bool error) {
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color:#ef4444;")
                                       : QStringLiteral("color:#7c8fa6;"));
    m_statusLabel->setText(msg);
}

void DriveWindow::importTune() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Importer tune ECU Drive"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("Tune ECU Drive (*.ecutune *.zip);;Tous (*.*)"));
    if (path.isEmpty()) return;
    loadTuneFile(path);
}

void DriveWindow::loadTuneFile(const QString& path) {
    auto pkg = ecu::TunePackageIo::readZipFile(path);
    if (!pkg) {
        QMessageBox::warning(this, tr("Import"), pkg.error());
        return;
    }
    applyTunePackage(*pkg, path);
}

void DriveWindow::applyTunePackage(const ecu::TunePackage& pkg, const QString& path) {
    if (!m_validator.loadTunePackage(pkg)) {
        // Peut échouer si fingerprints absents — on charge quand même les meta.
        m_validator.loadRomWithRecipe(pkg.rom, pkg.manifest.ecuId, pkg.recipeJson);
    }
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
    m_sessionBtn->setEnabled(m_connected && m_validator.isReady());
    setStatus(m_validator.isReady()
                  ? tr("Tune chargé — connecte l'ELM327.")
                  : tr("Tune chargé mais aucune map validable (ROM/recipe)."),
              !m_validator.isReady());
}

void DriveWindow::refreshPorts() {
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
}

void DriveWindow::startBtScan() {
#if defined(ELM_HAVE_BLUETOOTH)
    if (!m_btCombo) return;
    ensureBtAgent();
    if (!m_btAgent) return;
    m_btCombo->clear();
    m_scanBtBtn->setEnabled(false);
    setStatus(tr("Scan Bluetooth classique (modules ELM327 bleus)…\n"
                 "Appaire d’abord le dongle (PIN 1234 ou 0000) si besoin."));
    m_btAgent->start(QBluetoothDeviceDiscoveryAgent::ClassicMethod);
#else
    setStatus(tr("Bluetooth non disponible dans ce build."), true);
#endif
}

void DriveWindow::toggleConnect() {
    if (m_connected) {
        m_elm->disconnectPort();
        return;
    }
#if defined(ELM_HAVE_BLUETOOTH)
    if (m_btCombo && m_btCombo->currentIndex() >= 0
        && !m_btCombo->currentData().toString().isEmpty()
        && (m_portCombo->currentData().toString().isEmpty()
            || QMessageBox::question(this, tr("Connexion"),
                   tr("Utiliser Bluetooth (%1) ?\nOui = BT, Non = USB.")
                       .arg(m_btCombo->currentText()),
                   QMessageBox::Yes | QMessageBox::No)
                   == QMessageBox::Yes)) {
        const QString addr = m_btCombo->currentData().toString();
#if defined(Q_OS_ANDROID) || defined(Q_OS_LINUX)
        QBluetoothLocalDevice local;
        if (local.isValid()) {
            const auto paired = local.pairingStatus(QBluetoothAddress(addr));
            if (paired == QBluetoothLocalDevice::Unpaired) {
                setStatus(tr("Module non appairé. Dans les réglages Bluetooth du "
                             "téléphone, appairer le dongle bleu (PIN 1234 ou 0000), "
                             "puis réessaie."), true);
                return;
            }
        }
#endif
        m_elm->connectBluetooth(addr);
        return;
    }
#endif
    const QString port = m_portCombo->currentData().toString();
    if (port.isEmpty()) {
        setStatus(tr("Choisis un port USB ou un appareil Bluetooth."), true);
        return;
    }
    m_elm->connectPort(port, 0);
}

void DriveWindow::toggleSession() {
    if (m_sessionOn) stopSession();
    else startSession();
}

void DriveWindow::startSession() {
    if (!m_connected || !m_validator.isReady() || m_sessionOn) return;
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
    autoStartCsv();
    m_verdict->setText(tr("Acquisition…"));
    setStatus(tr("Session conduite active."));
}

void DriveWindow::stopSession() {
    if (!m_sessionOn) return;
    m_elm->stopPolling();
    m_sessionOn = false;
    autoStopCsv();
    const auto sum = m_session.finish();
    m_sessionBtn->setText(tr("▶  Lancer session conduite"));
    if (sum.ticks > 0) showSummary(sum);
}

void DriveWindow::onPid(quint8 pid, double value, const QString&, const QString&) {
    m_live[pid] = value;
    if (m_sessionOn) runValidation();
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

void DriveWindow::maybeAlert() {
    const int streak = m_hyst.failStreak();
    if (streak < 3 || streak == m_lastAlertAt || streak % 3 != 0) return;
    m_lastAlertAt = streak;
    if (m_beepChk->isChecked()) QApplication::beep();
}

void DriveWindow::autoStartCsv() {
    if (m_csv) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/datalog");
    QDir().mkpath(dir);
    m_lastCsv = dir + QStringLiteral("/drive_%1.csv")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    m_csv = new QFile(m_lastCsv, this);
    if (!m_csv->open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_csv->deleteLater(); m_csv = nullptr;
        m_csvLabel->setText(tr("CSV : échec"));
        return;
    }
    m_session.setCsvPath(m_lastCsv);
    QTextStream(m_csv) << "time,map,measured,expected,delta,unit,status,rpm,load\n";
    m_csvLabel->setText(tr("CSV : %1").arg(QFileInfo(m_lastCsv).fileName()));
}

void DriveWindow::autoStopCsv() {
    if (!m_csv) return;
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
}

void DriveWindow::showSummary(const ecu::SessionSummary& sum) {
    QString hot;
    for (const auto& h : sum.hotspots)
        hot += tr("\n  (%1,%2) ×%3 |Δ|%4")
                   .arg(h.gx).arg(h.gy).arg(h.count)
                   .arg(h.meanAbsDelta(), 0, 'f', 1);
    QMessageBox::information(this, tr("Fin de session"),
        tr("Ticks %1\nOK %2 · Warn %3 · Fail %4\nDans tolérance : %5 %\n"
           "Pic |Δ| %6 sur %7\nHotspots:%8\n\nCSV : %9")
            .arg(sum.ticks).arg(sum.ok).arg(sum.warn).arg(sum.fail)
            .arg(sum.okRatio(), 0, 'f', 1)
            .arg(sum.peakAbsDelta, 0, 'f', 1)
            .arg(sum.peakMap.isEmpty() ? QStringLiteral("—") : sum.peakMap)
            .arg(hot.isEmpty() ? tr("\n  (aucun)") : hot)
            .arg(sum.csvPath.isEmpty() ? m_lastCsv : sum.csvPath));
}

void DriveWindow::shareLastLog() {
    if (m_lastCsv.isEmpty() || !QFile::exists(m_lastCsv)) {
        setStatus(tr("Aucun log à partager."), true);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_lastCsv).absolutePath()));
    setStatus(tr("Dossier logs ouvert — partage le CSV."));
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
    m_updateProgress->setValue(int(m_updater->progress() * 100));
    m_updateActionBtn->setVisible(st != S::Downloading);
    m_updateDismissBtn->setVisible(st != S::Downloading);

    if (st == S::Downloading) {
        m_updateTitle->setText(tr("Téléchargement %1…").arg(m_updater->latestVersion()));
        m_updateSub->setText(tr("%1 %").arg(int(m_updater->progress() * 100)));
    } else if (st == S::Ready) {
        m_updateTitle->setText(tr("Version %1 prête").arg(m_updater->latestVersion()));
        m_updateSub->setText(tr("Tu as la %1").arg(m_updater->currentVersion()));
        m_updateActionBtn->setText(tr("Installer"));
    } else if (st == S::Failed) {
        m_updateTitle->setText(tr("Échec de la mise à jour"));
        m_updateSub->setText(tr("Réessaie ou ouvre la page GitHub."));
        m_updateActionBtn->setText(tr("Réessayer"));
    } else {
        m_updateTitle->setText(tr("Version %1 disponible").arg(m_updater->latestVersion()));
        m_updateSub->setText(tr("Tu as la %1").arg(m_updater->currentVersion()));
        m_updateActionBtn->setText(tr("Mettre à jour"));
    }
}

void DriveWindow::onUpdateAction() {
    if (!m_updater) return;
    using S = Updater::State;
    const S st = m_updater->state();
    if (st == S::Ready) {
        m_updater->install();
        return;
    }
    if (st == S::Failed) {
        m_updater->check();
        return;
    }
    if (st == S::Available) {
        if (!m_updater->releaseNotes().isEmpty()) {
            const auto r = QMessageBox::information(
                this, tr("Nouveautés — %1").arg(m_updater->latestVersion()),
                m_updater->releaseNotes(),
                QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
            if (r != QMessageBox::Ok) return;
        }
        m_updater->download();
    }
}

void DriveWindow::onUpdateDismiss() {
    if (m_updater) m_updater->dismiss();
}

void DriveWindow::checkUpdatesManual() {
    if (!m_updater) return;
    setStatus(tr("Vérification des mises à jour…"));
    m_updater->check();
}

} // namespace ecu_drive
