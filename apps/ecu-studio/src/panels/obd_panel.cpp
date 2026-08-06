#include "obd_panel.h"
#include "obd/elm327.h"
#include "obd/can_tune_validator.h"
#include "../rom_document.h"
#include "ecu/Obd2.hpp"
#include "ecu/TuneValidation.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QFile>
#include <QFileDialog>
#include <QDateTime>
#include <QTextStream>
#include <QClipboard>
#include <QApplication>
#include <QTimer>
#include <QTabWidget>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QBrush>
#include <QFrame>
#include <QSettings>
#include <QStandardPaths>
#include <QFileInfo>

#include <optional>

namespace ecu_studio {

namespace {
constexpr int kDtcStored  = 1;
constexpr int kDtcPending = 2;

QString statusLabel(ecu::ValidationStatus s) {
    switch (s) {
        case ecu::ValidationStatus::Ok:     return ObdPanel::tr("OK");
        case ecu::ValidationStatus::Warn:   return ObdPanel::tr("Attention");
        case ecu::ValidationStatus::Fail:   return ObdPanel::tr("Écart");
        case ecu::ValidationStatus::NoData: return ObdPanel::tr("—");
    }
    return QStringLiteral("?");
}

QColor statusColor(ecu::ValidationStatus s) {
    switch (s) {
        case ecu::ValidationStatus::Ok:     return QColor("#22c55e");
        case ecu::ValidationStatus::Warn:   return QColor("#f59e0b");
        case ecu::ValidationStatus::Fail:   return QColor("#ef4444");
        default:                            return QColor("#6b7280");
    }
}

} // namespace

ObdPanel::ObdPanel(RomDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc) {
    m_validator = new ecu::TuneValidator;
    m_elm = new Elm327(this);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ObdPanel::tryAutoReconnect);
    buildUi();
    loadSettings();
    applyDriveModeUi(m_driveMode);

    if (m_doc) {
        connect(m_doc, &RomDocument::romLoaded, this, &ObdPanel::refreshValidatorFromDoc);
        connect(m_doc, &RomDocument::ecuChanged, this, &ObdPanel::refreshValidatorFromDoc);
        connect(m_doc, &RomDocument::romModified, this, &ObdPanel::refreshValidatorFromDoc);
        refreshValidatorFromDoc();
    }

    connect(m_elm, &Elm327::connected, this, [this](const QString& v) {
        m_connected = true;
        m_wantConnected = true;
        m_reconnectTimer->stop();
        m_connectBtn->setEnabled(true);
        m_connectBtn->setText(tr("Déconnecter"));
        setStatus(tr("Connecté — %1").arg(v));
        m_datalogBtn->setEnabled(true);
        m_valBtn->setEnabled(m_validator->isReady());
        m_dtcReadBtn->setEnabled(true); m_dtcClearBtn->setEnabled(true);
        m_freezeBtn->setEnabled(true);
        m_vinBtn->setEnabled(true); m_canBtn->setEnabled(true);
        tryAutoStartDriveSession();
    });
    connect(m_elm, &Elm327::disconnected, this, [this]() {
        m_connected = false; m_datalog = false;
        stopValidation();
        m_canSniff = false;
        m_connectBtn->setEnabled(true);
        m_connectBtn->setText(tr("Connecter"));
        m_datalogBtn->setText(tr("Démarrer datalog"));
        m_driveBtn->setText(tr("▶  Lancer session conduite"));
        m_canBtn->setText(tr("Sniffer CAN (ATMA)"));
        m_datalogBtn->setEnabled(false);
        m_valBtn->setEnabled(false);
        m_dtcReadBtn->setEnabled(false); m_dtcClearBtn->setEnabled(false);
        m_freezeBtn->setEnabled(false);
        m_vinBtn->setEnabled(false); m_canBtn->setEnabled(false);
        if (m_wantConnected && m_autoReconnect->isChecked())
            scheduleAutoReconnect(tr("Lien perdu"));
        else
            setStatus(tr("Déconnecté."));
    });
    connect(m_elm, &Elm327::errorOccurred, this, [this](const QString& m) {
        m_connectBtn->setEnabled(true);
        m_connected = false;
        if (m_wantConnected && m_autoReconnect->isChecked())
            scheduleAutoReconnect(m);
        else {
            m_wantConnected = false;
            setStatus(m, true);
        }
    });
    connect(m_elm, &Elm327::status, this, [this](const QString& m) { setStatus(m); });
    connect(m_elm, &Elm327::rawLine, this, [this](const QString& l) { m_log->appendPlainText(l); });

    connect(m_elm, &Elm327::pidResult, this,
            [this](quint8 pid, double value, const QString& name, const QString& unit) {
        onPidUpdate(pid, value);
        const int row = m_pidRow.value(pid, -1);
        if (row >= 0)
            m_pidTable->item(row, 1)->setText(QString::number(value, 'f', 2));
        if (m_csv && !m_valCsv) {
            QTextStream ts(m_csv);
            ts << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
               << ',' << name << ',' << QString::number(value, 'f', 3) << ',' << unit << '\n';
        }
    });
    connect(m_elm, &Elm327::freezeFrameResult, this,
            [this](quint8 pid, double value, const QString& name, const QString& unit) {
        const int row = m_freezeTable->rowCount();
        m_freezeTable->insertRow(row);
        m_freezeTable->setItem(row, 0, new QTableWidgetItem(name));
        m_freezeTable->setItem(row, 1, new QTableWidgetItem(QString::number(value, 'f', 2)));
        m_freezeTable->setItem(row, 2, new QTableWidgetItem(unit));
    });
    connect(m_elm, &Elm327::pidUnsupported, this, [this](quint8 pid) {
        const int row = m_pidRow.value(pid, -1);
        if (row >= 0 && m_pidTable->item(row, 1)->text().isEmpty())
            m_pidTable->item(row, 1)->setText(QStringLiteral("—"));
    });
    connect(m_elm, &Elm327::dtcsReady, this,
            [this](const QStringList& codes, bool pending) {
        mergeDtcCodes(codes, pending);
        if (m_dtcAwaiting > 0) --m_dtcAwaiting;
        if (m_dtcAwaiting == 0) {
            refreshDtcTable();
            const int n = m_dtcFlags.size();
            setStatus(n == 0 ? tr("Aucun code défaut")
                             : tr("%1 code(s) défaut (modes 03 + 07)").arg(n));
            m_dtcCopyBtn->setEnabled(n > 0);
            m_dtcExportBtn->setEnabled(n > 0);
        }
    });
    connect(m_elm, &Elm327::vinReady, this, [this](const QString& vin) {
        m_vinLabel->setText(vin.isEmpty() ? tr("VIN indisponible") : vin);
    });
    connect(m_elm, &Elm327::canFrame, this, [this](quint32 id, QByteArray data) {
        int row = m_canRow.value(id, -1);
        if (row < 0) {
            row = m_canTable->rowCount();
            m_canTable->insertRow(row);
            m_canTable->setItem(row, 0, new QTableWidgetItem(
                QStringLiteral("0x%1").arg(id, 3, 16, QLatin1Char('0')).toUpper()));
            m_canTable->setItem(row, 1, new QTableWidgetItem);
            m_canTable->setItem(row, 2, new QTableWidgetItem);
            m_canRow.insert(id, row);
        }
        m_canTable->item(row, 1)->setText(QString::number(data.size()));
        m_canTable->item(row, 2)->setText(QString::fromLatin1(data.toHex(' ').toUpper()));
    });

    refreshPorts();
}

ObdPanel::~ObdPanel() {
    saveSettings();
    stopValidation();
    autoStopCsv();
    delete m_validator;
}

void ObdPanel::refreshValidatorFromDoc() {
    if (!m_doc || !m_doc->isLoaded() || m_doc->ecuId().isEmpty()) {
        m_validator->clear();
        if (m_romInfoLabel)
            m_romInfoLabel->setText(tr("Charge une ROM avec recipe OpenDAMOS pour valider le tune."));
        m_valBtn->setEnabled(false);
        return;
    }
    const bool ok = m_validator->loadRom(m_doc->rom(), m_doc->ecuId());
    if (m_romInfoLabel) {
        m_romInfoLabel->setText(ok
            ? tr("Tune : %1 — MD5 %2 — %3 map(s) surveillée(s)")
                  .arg(m_doc->name(), m_validator->romMd5().left(8))
                  .arg(static_cast<int>(m_validator->rules().size()))
            : tr("Impossible de charger OpenDAMOS pour « %1 ».").arg(m_doc->ecuId()));
    }
    m_valBtn->setEnabled(ok && m_connected);
    if (ok && m_connected) tryAutoStartDriveSession();
}

void ObdPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto* connBox = new QGroupBox(tr("Adaptateur ELM327 (USB)"), this);
    auto* cl = new QHBoxLayout(connBox);
    cl->addWidget(new QLabel(tr("Port :"), this));
    m_portCombo = new QComboBox(this); m_portCombo->setMinimumWidth(220);
    cl->addWidget(m_portCombo, 1);
    m_refreshBtn = new QPushButton(tr("↻"), this);
    cl->addWidget(m_refreshBtn);
    cl->addWidget(new QLabel(tr("Débit :"), this));
    m_baudCombo = new QComboBox(this);
    m_baudCombo->addItem(tr("Auto"), 0);
    m_baudCombo->addItem(QStringLiteral("38400"), 38400);
    m_baudCombo->addItem(QStringLiteral("115200"), 115200);
    cl->addWidget(m_baudCombo);
    m_connectBtn = new QPushButton(tr("Connecter"), this);
    m_connectBtn->setObjectName("accentBtn");
    cl->addWidget(m_connectBtn);
    m_autoReconnect = new QCheckBox(tr("Auto-reco"), this);
    m_autoReconnect->setChecked(true);
    m_autoReconnect->setToolTip(tr("Reconnexion automatique si le lien USB tombe."));
    cl->addWidget(m_autoReconnect);
    m_driveModeChk = new QCheckBox(tr("Mode conduite"), this);
    m_driveModeChk->setChecked(true);
    m_driveModeChk->setToolTip(tr(
        "Semi-automatique : gros affichage turbo, validation + log CSV auto "
        "dès la connexion. Idéal au volant (1 bouton avant de rouler)."));
    cl->addWidget(m_driveModeChk);
    root->addWidget(connBox);

    m_driveBtn = new QPushButton(tr("▶  Lancer session conduite"), this);
    m_driveBtn->setObjectName("accentBtn");
    m_driveBtn->setMinimumHeight(52);
    QFont df = m_driveBtn->font();
    df.setPointSizeF(df.pointSizeF() + 4.0);
    df.setBold(true);
    m_driveBtn->setFont(df);
    m_driveBtn->setToolTip(tr(
        "Connecte l'ELM327, démarre la validation tune et enregistre un CSV "
        "automatiquement — sans autre action pendant la route."));
    root->addWidget(m_driveBtn);

    buildDrivePanel(root);

    m_statusLabel = new QLabel(tr("Branche l'adaptateur, choisis le port, puis « Connecter »."), this);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#7c8fa6;"));
    root->addWidget(m_statusLabel);

    m_romInfoLabel = new QLabel(this);
    m_romInfoLabel->setStyleSheet(QStringLiteral("color:#60a5fa; font-size:11px;"));
    root->addWidget(m_romInfoLabel);

    m_tabs = new QTabWidget(this);

    // ── Onglet Live ──────────────────────────────────────────────────────────
    auto* livePage = new QWidget(this);
    auto* liveLay = new QVBoxLayout(livePage);
    m_pidTable = new QTableWidget(livePage);
    m_pidTable->setColumnCount(3);
    m_pidTable->setHorizontalHeaderLabels({ tr("Paramètre"), tr("Valeur"), tr("Unité") });
    m_pidTable->verticalHeader()->setVisible(false);
    m_pidTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pidTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    const auto& pids = ecu::obd2::livePids();
    m_pidTable->setRowCount(static_cast<int>(pids.size()));
    for (int i = 0; i < pids.size(); ++i) {
        m_pidTable->setItem(i, 0, new QTableWidgetItem(QString::fromUtf8(pids[i].name)));
        m_pidTable->setItem(i, 1, new QTableWidgetItem);
        m_pidTable->setItem(i, 2, new QTableWidgetItem(QString::fromUtf8(pids[i].unit)));
        m_pidRow.insert(pids[i].pid, i);
    }
    liveLay->addWidget(m_pidTable);
    auto* lbtn = new QHBoxLayout;
    m_datalogBtn = new QPushButton(tr("Démarrer datalog"), livePage);
    m_datalogBtn->setEnabled(false);
    m_csvBtn = new QPushButton(tr("Log CSV…"), livePage);
    lbtn->addWidget(m_datalogBtn); lbtn->addWidget(m_csvBtn); lbtn->addStretch();
    liveLay->addLayout(lbtn);
    m_tabs->addTab(livePage, tr("Live"));

    // ── Onglet Validation tune ───────────────────────────────────────────────
    auto* valPage = new QWidget(this);
    auto* valLay = new QVBoxLayout(valPage);
    auto* valCtl = new QHBoxLayout;
    m_tolSpin = new QDoubleSpinBox(valPage);
    m_tolSpin->setRange(1, 500);
    m_tolSpin->setValue(50);
    m_tolSpin->setSuffix(tr(" mbar"));
    valCtl->addWidget(new QLabel(tr("Tolérance ±"), valPage));
    valCtl->addWidget(m_tolSpin);
    m_yAxisCombo = new QComboBox(valPage);
    m_yAxisCombo->addItem(tr("Charge moteur % → axe Y"), static_cast<int>(ecu::YAxisMode::EngineLoadPct));
    m_yAxisCombo->addItem(tr("OpenDAMOS axe Y"), static_cast<int>(ecu::YAxisMode::OpenDamosAxis));
    valCtl->addWidget(new QLabel(tr("Axe Y :"), valPage));
    valCtl->addWidget(m_yAxisCombo);
    valCtl->addStretch();
    valLay->addLayout(valCtl);

    m_valTable = new QTableWidget(valPage);
    m_valTable->setColumnCount(6);
    m_valTable->setHorizontalHeaderLabels({
        tr("Map"), tr("Mesuré"), tr("Attendu"), tr("Δ"), tr("Unité"), tr("Statut")
    });
    m_valTable->verticalHeader()->setVisible(false);
    m_valTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_valTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    valLay->addWidget(m_valTable, 1);

    auto* vbtn = new QHBoxLayout;
    m_valBtn = new QPushButton(tr("Démarrer validation tune"), valPage);
    m_valBtn->setObjectName("accentBtn");
    m_valBtn->setEnabled(false);
    m_show3dBtn = new QPushButton(tr("Voir sur map 3D"), valPage);
    m_replayBtn = new QPushButton(tr("Replay CSV…"), valPage);
    vbtn->addWidget(m_valBtn);
    vbtn->addWidget(m_show3dBtn);
    vbtn->addWidget(m_replayBtn);
    vbtn->addStretch();
    valLay->addLayout(vbtn);

    m_canVal = new CanTuneValidator(valPage);
    valLay->addWidget(new QLabel(tr("Validation CAN avancée (SocketSpy MCP) :"), valPage));
    valLay->addWidget(m_canVal);
    m_tabs->addTab(valPage, tr("Validation tune"));

    // ── Onglet Diagnostic ────────────────────────────────────────────────────
    auto* diagPage = new QWidget(this);
    auto* dl = new QVBoxLayout(diagPage);
    auto* drow = new QHBoxLayout;
    m_dtcReadBtn = new QPushButton(tr("Lire DTC"), diagPage); m_dtcReadBtn->setEnabled(false);
    m_dtcClearBtn = new QPushButton(tr("Effacer DTC"), diagPage); m_dtcClearBtn->setEnabled(false);
    m_freezeBtn = new QPushButton(tr("Freeze frame"), diagPage); m_freezeBtn->setEnabled(false);
    m_vinBtn = new QPushButton(tr("Lire VIN"), diagPage); m_vinBtn->setEnabled(false);
    drow->addWidget(m_dtcReadBtn); drow->addWidget(m_dtcClearBtn);
    drow->addWidget(m_freezeBtn); drow->addWidget(m_vinBtn);
    dl->addLayout(drow);

    m_dtcTable = new QTableWidget(diagPage);
    m_dtcTable->setColumnCount(3);
    m_dtcTable->setHorizontalHeaderLabels({ tr("Code"), tr("Famille"), tr("Statut") });
    m_dtcTable->verticalHeader()->setVisible(false);
    m_dtcTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dtcTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_dtcTable->setMaximumHeight(120);
    dl->addWidget(m_dtcTable);

    auto* dtcBtn = new QHBoxLayout;
    m_dtcCopyBtn = new QPushButton(tr("Copier"), diagPage); m_dtcCopyBtn->setEnabled(false);
    m_dtcExportBtn = new QPushButton(tr("Exporter…"), diagPage); m_dtcExportBtn->setEnabled(false);
    dtcBtn->addWidget(m_dtcCopyBtn); dtcBtn->addWidget(m_dtcExportBtn); dtcBtn->addStretch();
    dl->addLayout(dtcBtn);

    m_vinLabel = new QLabel(tr("VIN : —"), diagPage);
    m_vinLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dl->addWidget(m_vinLabel);

    m_freezeTable = new QTableWidget(diagPage);
    m_freezeTable->setColumnCount(3);
    m_freezeTable->setHorizontalHeaderLabels({ tr("Paramètre"), tr("Valeur"), tr("Unité") });
    m_freezeTable->verticalHeader()->setVisible(false);
    m_freezeTable->setMaximumHeight(120);
    dl->addWidget(new QLabel(tr("Freeze frame (mode 02, trame 0) :"), diagPage));
    dl->addWidget(m_freezeTable);

    m_canBtn = new QPushButton(tr("Sniffer CAN (ATMA)"), diagPage);
    m_canBtn->setEnabled(false);
    dl->addWidget(m_canBtn);
    m_canTable = new QTableWidget(diagPage);
    m_canTable->setColumnCount(3);
    m_canTable->setHorizontalHeaderLabels({ tr("ID"), tr("DLC"), tr("Données") });
    m_canTable->verticalHeader()->setVisible(false);
    m_canTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    dl->addWidget(m_canTable, 1);
    m_tabs->addTab(diagPage, tr("Diagnostic"));

    root->addWidget(m_tabs, 1);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setFixedHeight(90);
    m_log->setStyleSheet(QStringLiteral("background:#111827; color:#9ca3af; font-family:monospace;"));
    root->addWidget(m_log);

    connect(m_refreshBtn, &QPushButton::clicked, this, &ObdPanel::refreshPorts);
    connect(m_connectBtn, &QPushButton::clicked, this, &ObdPanel::toggleConnect);
    connect(m_autoReconnect, &QCheckBox::toggled, this, &ObdPanel::onAutoReconnectToggled);
    connect(m_datalogBtn, &QPushButton::clicked, this, &ObdPanel::toggleDatalog);
    connect(m_valBtn, &QPushButton::clicked, this, &ObdPanel::toggleValidation);
    connect(m_canBtn, &QPushButton::clicked, this, &ObdPanel::toggleCanSniff);
    connect(m_csvBtn, &QPushButton::clicked, this, &ObdPanel::toggleCsv);
    connect(m_dtcReadBtn, &QPushButton::clicked, this, &ObdPanel::readDtcs);
    connect(m_dtcClearBtn, &QPushButton::clicked, this, &ObdPanel::clearDtcs);
    connect(m_freezeBtn, &QPushButton::clicked, this, &ObdPanel::readFreezeFrame);
    connect(m_dtcCopyBtn, &QPushButton::clicked, this, &ObdPanel::copyDtcs);
    connect(m_dtcExportBtn, &QPushButton::clicked, this, &ObdPanel::exportDtcs);
    connect(m_vinBtn, &QPushButton::clicked, this, [this]() { m_elm->readVin(); });
    connect(m_tolSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &ObdPanel::onToleranceChanged);
    connect(m_yAxisCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ObdPanel::onYAxisModeChanged);
    connect(m_show3dBtn, &QPushButton::clicked, this, &ObdPanel::onShowMap3d);
    connect(m_replayBtn, &QPushButton::clicked, this, &ObdPanel::replayValidationCsv);
    connect(m_valTable, &QTableWidget::cellClicked, this, [this](int row, int) {
        m_focusValRow = row;
    });
    connect(m_driveModeChk, &QCheckBox::toggled, this, &ObdPanel::onDriveModeToggled);
    connect(m_driveBtn, &QPushButton::clicked, this, &ObdPanel::onDriveSessionClicked);
}

void ObdPanel::buildDrivePanel(QVBoxLayout* root) {
    m_drivePanel = new QFrame(this);
    m_drivePanel->setFrameShape(QFrame::StyledPanel);
    m_drivePanel->setStyleSheet(QStringLiteral(
        "QFrame { background:#0f1520; border:1px solid #1e293b; border-radius:8px; }"));
    auto* dl = new QVBoxLayout(m_drivePanel);
    dl->setContentsMargins(12, 12, 12, 12);
    dl->setSpacing(8);

    m_driveBanner = new QFrame(m_drivePanel);
    m_driveBanner->setMinimumHeight(72);
    m_driveBanner->setStyleSheet(QStringLiteral(
        "QFrame { background:#1e293b; border-radius:8px; }"));
    auto* bl = new QVBoxLayout(m_driveBanner);
    bl->setContentsMargins(16, 8, 16, 8);
    m_driveVerdict = new QLabel(tr("Prêt — appuie sur ▶ Lancer"), m_driveBanner);
    QFont vf = m_driveVerdict->font();
    vf.setPointSizeF(20.0);
    vf.setBold(true);
    m_driveVerdict->setFont(vf);
    m_driveVerdict->setAlignment(Qt::AlignCenter);
    m_driveVerdict->setStyleSheet(QStringLiteral("color:#e6edf3;"));
    bl->addWidget(m_driveVerdict);
    dl->addWidget(m_driveBanner);

    m_boostBig = new QLabel(tr("— / — mbar"), m_drivePanel);
    QFont bf = m_boostBig->font();
    bf.setPointSizeF(28.0);
    bf.setBold(true);
    m_boostBig->setFont(bf);
    m_boostBig->setAlignment(Qt::AlignCenter);
    m_boostBig->setStyleSheet(QStringLiteral("color:#60a5fa;"));
    dl->addWidget(m_boostBig);

    m_boostSub = new QLabel(tr("Δ — mbar"), m_drivePanel);
    QFont sf = m_boostSub->font();
    sf.setPointSizeF(16.0);
    m_boostSub->setFont(sf);
    m_boostSub->setAlignment(Qt::AlignCenter);
    m_boostSub->setStyleSheet(QStringLiteral("color:#9ca3af;"));
    dl->addWidget(m_boostSub);

    m_rpmLoadLabel = new QLabel(tr("RPM —  ·  Charge — %"), m_drivePanel);
    m_rpmLoadLabel->setAlignment(Qt::AlignCenter);
    m_rpmLoadLabel->setStyleSheet(QStringLiteral("color:#7c8fa6; font-size:14px;"));
    dl->addWidget(m_rpmLoadLabel);

    m_csvDriveLabel = new QLabel(tr("Log CSV : inactif"), m_drivePanel);
    m_csvDriveLabel->setAlignment(Qt::AlignCenter);
    m_csvDriveLabel->setStyleSheet(QStringLiteral("color:#64748b; font-size:11px;"));
    dl->addWidget(m_csvDriveLabel);

    root->addWidget(m_drivePanel, 1);
}

void ObdPanel::loadSettings() {
    QSettings s;
    s.beginGroup(QStringLiteral("obd"));
    m_driveMode = s.value(QStringLiteral("driveMode"), true).toBool();
    if (m_driveModeChk) m_driveModeChk->setChecked(m_driveMode);
    if (m_autoReconnect) m_autoReconnect->setChecked(
        s.value(QStringLiteral("autoReconnect"), true).toBool());
    m_lastPort = s.value(QStringLiteral("lastPort")).toString();
    s.endGroup();
}

void ObdPanel::saveSettings() {
    QSettings s;
    s.beginGroup(QStringLiteral("obd"));
    s.setValue(QStringLiteral("driveMode"), m_driveMode);
    if (m_autoReconnect) s.setValue(QStringLiteral("autoReconnect"), m_autoReconnect->isChecked());
    if (!m_lastPort.isEmpty()) s.setValue(QStringLiteral("lastPort"), m_lastPort);
    s.endGroup();
}

void ObdPanel::applyDriveModeUi(bool on) {
    if (m_tabs)       m_tabs->setVisible(!on);
    if (m_log)        m_log->setVisible(!on);
    if (m_drivePanel) m_drivePanel->setVisible(on);
    if (m_statusLabel) m_statusLabel->setVisible(!on);
    if (m_driveBtn) {
        m_driveBtn->setVisible(on);
        if (m_validating)
            m_driveBtn->setText(tr("■  Arrêter session"));
    }
    if (m_romInfoLabel) m_romInfoLabel->setVisible(!on);
}

void ObdPanel::onDriveModeToggled(bool on) {
    m_driveMode = on;
    applyDriveModeUi(on);
    saveSettings();
    if (on && m_connected) tryAutoStartDriveSession();
}

void ObdPanel::onDriveSessionClicked() {
    if (m_validating) {
        stopValidation();
        if (m_connected && m_wantConnected) {
            m_wantConnected = false;
            m_elm->disconnectPort();
        }
        return;
    }
    m_driveMode = true;
    if (m_driveModeChk) m_driveModeChk->setChecked(true);
    applyDriveModeUi(true);
    if (!m_connected) startConnect();
    else tryAutoStartDriveSession();
}

void ObdPanel::tryAutoStartDriveSession() {
    if (!m_driveMode || !m_connected || !m_validator->isReady() || m_validating)
        return;
    startValidation();
}

void ObdPanel::startValidation() {
    if (!m_connected || !m_validator->isReady() || m_validating) return;
    if (m_datalog) toggleDatalog();
    if (m_canSniff) toggleCanSniff();
    if (!m_driveMode) m_tabs->setCurrentIndex(1);

    // Map boost en priorité — focus auto sur la 1ʳᵉ règle boost.
    for (int i = 0; i < static_cast<int>(m_validator->rules().size()); ++i) {
        if (m_validator->rules()[static_cast<std::size_t>(i)].category == "boost") {
            m_focusValRow = i;
            break;
        }
    }

    const QList<std::uint8_t> pids = { 0x0C, 0x04, 0x0B, 0x33, 0x10, 0x0D };
    m_elm->startPolling(pids, 180);
    m_validating = true;
    m_failStreak = 0;
    if (m_valBtn) m_valBtn->setText(tr("Arrêter validation tune"));
    if (m_driveBtn) m_driveBtn->setText(tr("■  Arrêter session"));
    if (m_driveMode) autoStartCsv();
    setStatus(tr("Session conduite active — regarde le bandeau turbo."));
    if (m_driveVerdict)
        m_driveVerdict->setText(tr("Acquisition…"));
}

void ObdPanel::stopValidation() {
    if (!m_validating) return;
    m_elm->stopPolling();
    m_validating = false;
    m_failStreak = 0;
    if (m_valBtn) m_valBtn->setText(tr("Démarrer validation tune"));
    if (m_driveBtn) m_driveBtn->setText(tr("▶  Lancer session conduite"));
    if (m_driveMode) autoStopCsv();
}

void ObdPanel::autoStartCsv() {
    if (m_csv) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/datalog");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/drive_%1.csv")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    m_csv = new QFile(path, this);
    if (!m_csv->open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_csv->deleteLater(); m_csv = nullptr;
        if (m_csvDriveLabel) m_csvDriveLabel->setText(tr("Log CSV : échec écriture"));
        return;
    }
    m_valCsv = true;
    QTextStream(m_csv) << "time,map,measured,expected,delta,unit,status,rpm,load\n";
    if (m_csvDriveLabel)
        m_csvDriveLabel->setText(tr("Log CSV : %1").arg(QFileInfo(path).fileName()));
    if (m_csvBtn) m_csvBtn->setText(tr("Arrêter CSV"));
}

void ObdPanel::autoStopCsv() {
    if (!m_csv) return;
    const QString path = m_csv->fileName();
    m_csv->close();
    m_csv->deleteLater();
    m_csv = nullptr;
    m_valCsv = false;
    if (m_csvBtn) m_csvBtn->setText(tr("Log CSV…"));
    if (m_csvDriveLabel)
        m_csvDriveLabel->setText(tr("Log CSV : %1 (terminé)").arg(QFileInfo(path).fileName()));
}

ecu::LivePidSnapshot ObdPanel::liveSnapshot() const {
    ecu::LivePidSnapshot snap;
    for (auto it = m_liveValues.constBegin(); it != m_liveValues.constEnd(); ++it)
        snap[it.key()] = it.value();
    return snap;
}

void ObdPanel::onPidUpdate(quint8 pid, double value) {
    m_liveValues[pid] = value;
    if (m_validating) runValidation();
}

void ObdPanel::runValidation() {
    if (!m_validator->isReady()) return;
    const auto results = m_validator->evaluateAll(liveSnapshot());
    updateValidationTable(results);
    if (m_driveMode) updateDriveDashboard(results);
    appendValidationCsv(results);

    if (const auto primary = primaryBoostResult(results)) {
        if (primary->status != ecu::ValidationStatus::NoData && primary->mapAddress > 0) {
            emit livePointUpdated(static_cast<quint32>(primary->mapAddress),
                                  primary->ix0, primary->iy0,
                                  primary->measured, primary->expected);
        }
    }
}

std::optional<ecu::ValidationResult> ObdPanel::primaryBoostResult(
    const std::vector<ecu::ValidationResult>& results) const {
    for (const auto& r : results) {
        if (r.mapName.contains(QStringLiteral("pAirBas"), Qt::CaseInsensitive)
            || r.mapName.contains(QStringLiteral("boost"), Qt::CaseInsensitive))
            return r;
    }
    for (const auto& r : results) {
        if (r.status != ecu::ValidationStatus::NoData) return r;
    }
    return std::nullopt;
}

void ObdPanel::updateDriveDashboard(const std::vector<ecu::ValidationResult>& results) {
    const double rpm = m_liveValues.value(0x0C, 0.0);
    const double load = m_liveValues.value(0x04, 0.0);
    if (m_rpmLoadLabel)
        m_rpmLoadLabel->setText(tr("RPM %1  ·  Charge %2 %")
                                    .arg(rpm > 0 ? QString::number(rpm, 'f', 0) : QStringLiteral("—"))
                                    .arg(load > 0 ? QString::number(load, 'f', 0) : QStringLiteral("—")));

    const auto boost = primaryBoostResult(results);
    if (!boost || boost->status == ecu::ValidationStatus::NoData) {
        if (m_driveVerdict) {
            m_driveVerdict->setText(rpm > 400 ? tr("En attente de données…") : tr("Ralenti — accélère pour tester"));
            m_driveBanner->setStyleSheet(QStringLiteral(
                "QFrame { background:#1e293b; border-radius:8px; }"));
        }
        return;
    }

    const QString unit = boost->unit.isEmpty() ? QStringLiteral("mbar") : boost->unit;
    if (m_boostBig)
        m_boostBig->setText(tr("%1 / %2 %3")
                                .arg(boost->measured, 0, 'f', 0)
                                .arg(boost->expected, 0, 'f', 0)
                                .arg(unit));
    if (m_boostSub)
        m_boostSub->setText(tr("Δ %1 %2")
                                .arg(boost->delta, 0, 'f', 0)
                                .arg(unit));

    QString bannerBg;
    QString verdict;
    QColor  bigColor;
    switch (boost->status) {
        case ecu::ValidationStatus::Ok:
            bannerBg = QStringLiteral("QFrame { background:#14532d; border-radius:8px; }");
            verdict  = tr("TURBO OK");
            bigColor = QColor("#4ade80");
            m_failStreak = 0;
            break;
        case ecu::ValidationStatus::Warn:
            bannerBg = QStringLiteral("QFrame { background:#78350f; border-radius:8px; }");
            verdict  = boost->delta < 0 ? tr("LÉGER UNDERBOOST") : tr("LÉGER OVERBOOST");
            bigColor = QColor("#fbbf24");
            break;
        case ecu::ValidationStatus::Fail:
            bannerBg = QStringLiteral("QFrame { background:#7f1d1d; border-radius:8px; }");
            verdict  = boost->delta < 0 ? tr("UNDERBOOST") : tr("OVERBOOST");
            bigColor = QColor("#f87171");
            ++m_failStreak;
            break;
        default:
            bannerBg = QStringLiteral("QFrame { background:#1e293b; border-radius:8px; }");
            verdict  = tr("—");
            bigColor = QColor("#60a5fa");
            break;
    }
    if (m_driveBanner) m_driveBanner->setStyleSheet(bannerBg);
    if (m_driveVerdict) {
        m_driveVerdict->setText(verdict);
        m_driveVerdict->setStyleSheet(QStringLiteral("color:#fff;"));
    }
    if (m_boostBig)
        m_boostBig->setStyleSheet(QStringLiteral("color:%1;").arg(bigColor.name()));
}

void ObdPanel::updateValidationTable(const std::vector<ecu::ValidationResult>& results) {
    m_valTable->setRowCount(static_cast<int>(results.size()));
    for (int i = 0; i < static_cast<int>(results.size()); ++i) {
        const auto& r = results[static_cast<std::size_t>(i)];
        m_valTable->setItem(i, 0, new QTableWidgetItem(r.mapName));
        m_valTable->setItem(i, 1, new QTableWidgetItem(
            r.status == ecu::ValidationStatus::NoData ? QStringLiteral("—")
                : QString::number(r.measured, 'f', 1)));
        m_valTable->setItem(i, 2, new QTableWidgetItem(
            r.status == ecu::ValidationStatus::NoData ? QStringLiteral("—")
                : QString::number(r.expected, 'f', 1)));
        m_valTable->setItem(i, 3, new QTableWidgetItem(
            r.status == ecu::ValidationStatus::NoData ? QStringLiteral("—")
                : QString::number(r.delta, 'f', 1)));
        m_valTable->setItem(i, 4, new QTableWidgetItem(r.unit));
        auto* st = new QTableWidgetItem(statusLabel(r.status));
        st->setForeground(QBrush(statusColor(r.status)));
        m_valTable->setItem(i, 5, st);
    }
}

void ObdPanel::appendValidationCsv(const std::vector<ecu::ValidationResult>& results) {
    if (!m_csv || !m_valCsv) return;
    QTextStream ts(m_csv);
    const QString tsStr = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    for (const auto& r : results) {
        ts << tsStr << ','
           << r.mapName << ','
           << (r.status == ecu::ValidationStatus::NoData ? QString() : QString::number(r.measured, 'f', 2))
           << ','
           << (r.status == ecu::ValidationStatus::NoData ? QString() : QString::number(r.expected, 'f', 2))
           << ','
           << (r.status == ecu::ValidationStatus::NoData ? QString() : QString::number(r.delta, 'f', 2))
           << ','
           << r.unit << ','
           << statusLabel(r.status) << ','
           << r.xPhys << ',' << r.yPhys << '\n';
    }
}

void ObdPanel::onToleranceChanged(double v) {
    m_validator->setToleranceMbar(v);
}

void ObdPanel::onYAxisModeChanged(int idx) {
    m_validator->setYAxisMode(static_cast<ecu::YAxisMode>(m_yAxisCombo->itemData(idx).toInt()));
    if (m_doc && m_doc->isLoaded()) refreshValidatorFromDoc();
}

void ObdPanel::onShowMap3d() {
    if (m_focusValRow < 0 || m_focusValRow >= m_valTable->rowCount()) {
        setStatus(tr("Sélectionne une map dans le tableau."), true);
        return;
    }
    const QString mapName = m_valTable->item(m_focusValRow, 0)->text();
    const auto snap = liveSnapshot();
    for (const auto& rule : m_validator->rules()) {
        if (QString::fromStdString(rule.mapName) != mapName) continue;
        if (auto r = m_validator->evaluateRule(rule, snap)) {
            const auto ent = m_validator->entry(rule.mapName);
            QString xU, yU, dU;
            if (ent) {
                if (!ent->axes.empty()) xU = QString::fromStdString(ent->axes[0].unit);
                if (ent->axes.size() >= 2) yU = QString::fromStdString(ent->axes[1].unit);
                dU = QString::fromStdString(ent->data.unit);
            }
            emit showMapOn3dRequested(static_cast<quint32>(r->mapAddress), mapName,
                                      r->xPhys, r->yPhys, r->measured, r->expected,
                                      xU, yU, dU);
            emit livePointUpdated(static_cast<quint32>(r->mapAddress),
                                  r->ix0, r->iy0, r->measured, r->expected);
            return;
        }
    }
}

void ObdPanel::replayValidationCsv() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Replay session validation"),
        {}, tr("CSV (*.csv)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(tr("Impossible d'ouvrir %1").arg(path), true);
        return;
    }

    int ok = 0, warn = 0, fail = 0, total = 0;
    QTextStream ts(&f);
    const QString header = ts.readLine();
    Q_UNUSED(header);

    while (!ts.atEnd()) {
        const QStringList cols = ts.readLine().split(QLatin1Char(','));
        if (cols.size() < 7) continue;
        ++total;
        const QString st = cols[6].trimmed();
        if (st == tr("OK") || st == QStringLiteral("OK")) ++ok;
        else if (st == tr("Attention") || st == QStringLiteral("Attention")) ++warn;
        else if (st == tr("Écart") || st == QStringLiteral("Écart")) ++fail;
    }

    QMessageBox::information(this, tr("Replay session"),
        tr("%1 échantillons analysés.\n\n"
           "OK : %2\nAttention : %3\nÉcart : %4\n\n"
           "Temps dans tolérance : %5 %")
            .arg(total).arg(ok).arg(warn).arg(fail)
            .arg(total > 0 ? QString::number(100.0 * ok / total, 'f', 1) : QStringLiteral("0")));
}

void ObdPanel::setStatus(const QString& msg, bool error) {
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color:#ef4444;")
                                       : QStringLiteral("color:#7c8fa6;"));
    m_statusLabel->setText(msg);
}

QString ObdPanel::dtcFamily(const QString& code) const {
    if (code.isEmpty()) return QString();
    switch (code[0].toLatin1()) {
        case 'P': return tr("Powertrain");
        case 'C': return tr("Chassis");
        case 'B': return tr("Body");
        case 'U': return tr("Network");
        default:  return QStringLiteral("?");
    }
}

QString ObdPanel::dtcStatusText(int flags) const {
    if ((flags & kDtcStored) && (flags & kDtcPending)) return tr("mémorisé + en attente");
    if (flags & kDtcPending) return tr("en attente");
    if (flags & kDtcStored)  return tr("mémorisé");
    return QStringLiteral("—");
}

void ObdPanel::mergeDtcCodes(const QStringList& codes, bool pending) {
    const int bit = pending ? kDtcPending : kDtcStored;
    for (const QString& c : codes)
        m_dtcFlags[c] = m_dtcFlags.value(c, 0) | bit;
}

void ObdPanel::refreshDtcTable() {
    m_dtcTable->setRowCount(0);
    QStringList keys = m_dtcFlags.keys();
    keys.sort();
    for (const QString& code : keys) {
        const int row = m_dtcTable->rowCount();
        m_dtcTable->insertRow(row);
        m_dtcTable->setItem(row, 0, new QTableWidgetItem(code));
        m_dtcTable->setItem(row, 1, new QTableWidgetItem(dtcFamily(code)));
        m_dtcTable->setItem(row, 2, new QTableWidgetItem(dtcStatusText(m_dtcFlags.value(code))));
    }
}

void ObdPanel::refreshPorts() {
    const QString keep = preferredPort();
    m_portCombo->clear();
    const auto ports = Elm327::listPorts();
    int preferIdx = -1;
    for (const auto& p : ports) {
        const QString label = (p.likelyElm ? QStringLiteral("★ ") : QString())
                              + p.port + (p.description.isEmpty() ? QString()
                                                                  : QStringLiteral("  (%1)").arg(p.description));
        m_portCombo->addItem(label, p.port);
        if (!keep.isEmpty() && p.port == keep) preferIdx = m_portCombo->count() - 1;
        else if (preferIdx < 0 && p.likelyElm) preferIdx = m_portCombo->count() - 1;
    }
    if (preferIdx >= 0) m_portCombo->setCurrentIndex(preferIdx);
}

QString ObdPanel::preferredPort() const {
    if (!m_lastPort.isEmpty()) return m_lastPort;
    if (m_portCombo && !m_portCombo->currentData().isNull())
        return m_portCombo->currentData().toString();
    return QString();
}

void ObdPanel::toggleConnect() {
    if (m_connected || m_wantConnected) {
        m_wantConnected = false;
        m_reconnectTimer->stop();
        m_elm->disconnectPort();
        m_connectBtn->setEnabled(true);
        m_connectBtn->setText(tr("Connecter"));
        setStatus(tr("Déconnecté."));
        return;
    }
    startConnect();
}

void ObdPanel::startConnect() {
    refreshPorts();
    if (m_portCombo->currentData().isNull()) {
        setStatus(tr("Choisis un port."), true);
        if (m_wantConnected && m_autoReconnect->isChecked())
            scheduleAutoReconnect(tr("Port absent"));
        return;
    }
    m_lastPort = m_portCombo->currentData().toString();
    m_wantConnected = true;
    m_connectBtn->setEnabled(false);
    setStatus(tr("Connexion…"));
    m_elm->connectPort(m_lastPort, m_baudCombo->currentData().toInt());
}

void ObdPanel::onAutoReconnectToggled(bool on) {
    if (!on) { m_reconnectTimer->stop(); return; }
    if (m_wantConnected && !m_connected) scheduleAutoReconnect(tr("Auto-reco activé"));
}

void ObdPanel::scheduleAutoReconnect(const QString& why) {
    if (!m_autoReconnect->isChecked() || !m_wantConnected) return;
    setStatus(tr("%1 — nouvelle tentative dans 2 s…").arg(why), true);
    m_reconnectTimer->start(2000);
}

void ObdPanel::tryAutoReconnect() {
    if (!m_autoReconnect->isChecked() || !m_wantConnected || m_connected) return;
    startConnect();
}

void ObdPanel::toggleDatalog() {
    if (!m_connected) return;
    if (m_datalog) {
        m_elm->stopPolling();
        m_datalog = false;
        m_datalogBtn->setText(tr("Démarrer datalog"));
    } else {
        if (m_validating) toggleValidation();
        QList<std::uint8_t> pids;
        for (const auto& p : ecu::obd2::livePids()) pids.push_back(p.pid);
        m_elm->startPolling(pids, 200);
        m_datalog = true;
        m_datalogBtn->setText(tr("Arrêter datalog"));
    }
}

void ObdPanel::toggleValidation() {
    if (m_validating) stopValidation();
    else startValidation();
}

void ObdPanel::toggleCanSniff() {
    if (!m_connected) return;
    if (m_canSniff) {
        m_elm->stopCanMonitor();
        m_canSniff = false;
        m_canBtn->setText(tr("Sniffer CAN (ATMA)"));
    } else {
        if (m_datalog) toggleDatalog();
        if (m_validating) toggleValidation();
        m_canTable->setRowCount(0); m_canRow.clear();
        m_elm->startCanMonitor();
        m_canSniff = true;
        m_canBtn->setText(tr("Arrêter sniff CAN"));
    }
}

void ObdPanel::toggleCsv() {
    if (m_csv) {
        m_csv->close(); m_csv->deleteLater(); m_csv = nullptr;
        m_valCsv = false;
        m_csvBtn->setText(tr("Log CSV…"));
        setStatus(tr("Log CSV arrêté."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Enregistrer le log"),
        QStringLiteral("validation_%1.csv")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))),
        tr("CSV (*.csv)"));
    if (path.isEmpty()) return;
    m_csv = new QFile(path, this);
    if (!m_csv->open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus(tr("Impossible d'écrire %1").arg(path), true);
        m_csv->deleteLater(); m_csv = nullptr; return;
    }
    m_valCsv = m_validating;
    if (m_valCsv)
        QTextStream(m_csv) << "time,map,measured,expected,delta,unit,status,rpm,load\n";
    else
        QTextStream(m_csv) << "time,parametre,valeur,unite\n";
    m_csvBtn->setText(tr("Arrêter CSV"));
    setStatus(tr("Log CSV : %1").arg(path));
}

void ObdPanel::readDtcs() {
    if (!m_connected) return;
    m_dtcFlags.clear();
    m_dtcTable->setRowCount(0);
    m_dtcCopyBtn->setEnabled(false);
    m_dtcExportBtn->setEnabled(false);
    m_dtcAwaiting = 2;
    setStatus(tr("Lecture DTC modes 03 + 07…"));
    m_elm->readDtcs(false);
    m_elm->readDtcs(true);
}

void ObdPanel::readFreezeFrame() {
    if (!m_connected) return;
    m_freezeTable->setRowCount(0);
    setStatus(tr("Lecture freeze frame (mode 02)…"));
    m_elm->readFreezeFrame();
}

void ObdPanel::clearDtcs() { if (m_connected) m_elm->clearDtcs(); }

void ObdPanel::copyDtcs() {
    QStringList lines;
    QStringList keys = m_dtcFlags.keys();
    keys.sort();
    for (const QString& code : keys)
        lines << QStringLiteral("%1\t%2\t%3")
                     .arg(code, dtcFamily(code), dtcStatusText(m_dtcFlags.value(code)));
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    setStatus(tr("%1 code(s) copié(s)").arg(lines.size()));
}

void ObdPanel::exportDtcs() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exporter les codes défaut"),
        QStringLiteral("dtc_%1.csv")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))),
        tr("CSV (*.csv);;Texte (*.txt)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus(tr("Impossible d'écrire %1").arg(path), true);
        return;
    }
    QTextStream ts(&f);
    const bool csv = path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive);
    if (csv) ts << "code,famille,statut\n";
    QStringList keys = m_dtcFlags.keys();
    keys.sort();
    for (const QString& code : keys) {
        if (csv)
            ts << code << ',' << dtcFamily(code) << ','
               << dtcStatusText(m_dtcFlags.value(code)) << '\n';
        else
            ts << code << '\t' << dtcFamily(code) << '\t'
               << dtcStatusText(m_dtcFlags.value(code)) << '\n';
    }
    setStatus(tr("Export DTC : %1").arg(path));
}

} // namespace ecu_studio
