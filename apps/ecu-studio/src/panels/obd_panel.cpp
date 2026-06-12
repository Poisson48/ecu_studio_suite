#include "obd_panel.h"
#include "obd/elm327.h"
#include "ecu/Obd2.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QFile>
#include <QFileDialog>
#include <QDateTime>
#include <QTextStream>

namespace ecu_studio {

ObdPanel::ObdPanel(QWidget* parent) : QWidget(parent) {
    m_elm = new Elm327(this);
    buildUi();

    connect(m_elm, &Elm327::connected, this, [this](const QString& v) {
        m_connected = true;
        m_connectBtn->setText(tr("Déconnecter"));
        setStatus(tr("Connecté — %1").arg(v));
        m_datalogBtn->setEnabled(true);
        m_dtcReadBtn->setEnabled(true); m_dtcClearBtn->setEnabled(true);
        m_vinBtn->setEnabled(true); m_canBtn->setEnabled(true);
    });
    connect(m_elm, &Elm327::disconnected, this, [this]() {
        m_connected = false; m_datalog = false; m_canSniff = false;
        m_connectBtn->setText(tr("Connecter"));
        m_datalogBtn->setText(tr("Démarrer datalog"));
        m_canBtn->setText(tr("Sniffer CAN"));
        m_datalogBtn->setEnabled(false);
        m_dtcReadBtn->setEnabled(false); m_dtcClearBtn->setEnabled(false);
        m_vinBtn->setEnabled(false); m_canBtn->setEnabled(false);
        setStatus(tr("Déconnecté."));
    });
    connect(m_elm, &Elm327::errorOccurred, this, [this](const QString& m) { setStatus(m, true); });
    connect(m_elm, &Elm327::status, this, [this](const QString& m) { setStatus(m); });
    connect(m_elm, &Elm327::rawLine, this, [this](const QString& l) { m_log->appendPlainText(l); });

    connect(m_elm, &Elm327::pidResult, this,
            [this](quint8 pid, double value, const QString& name, const QString& unit) {
        const int row = m_pidRow.value(pid, -1);
        if (row >= 0) {
            m_pidTable->item(row, 1)->setText(QString::number(value, 'f', 2));
        }
        if (m_csv) {
            QTextStream ts(m_csv);
            ts << QDateTime::currentDateTime().toString("HH:mm:ss.zzz") << ','
               << name << ',' << QString::number(value, 'f', 3) << ',' << unit << '\n';
        }
    });
    connect(m_elm, &Elm327::pidUnsupported, this, [this](quint8 pid) {
        const int row = m_pidRow.value(pid, -1);
        if (row >= 0 && m_pidTable->item(row, 1)->text().isEmpty())
            m_pidTable->item(row, 1)->setText(QStringLiteral("—"));
    });
    connect(m_elm, &Elm327::dtcsReady, this, [this](const QStringList& codes) {
        m_dtcLabel->setText(codes.isEmpty() ? tr("Aucun code défaut")
                                            : codes.join(QStringLiteral("  ")));
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

ObdPanel::~ObdPanel() { if (m_csv) { m_csv->close(); } }

void ObdPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8); root->setSpacing(8);

    // ── Connexion ────────────────────────────────────────────────────────────
    auto* connBox = new QGroupBox(tr("Adaptateur ELM327 (USB)"), this);
    auto* cl = new QHBoxLayout(connBox);
    cl->addWidget(new QLabel(tr("Port :"), this));
    m_portCombo = new QComboBox(this); m_portCombo->setMinimumWidth(260);
    cl->addWidget(m_portCombo, 1);
    m_refreshBtn = new QPushButton(tr("↻"), this);
    m_refreshBtn->setToolTip(tr("Rafraîchir la liste des ports"));
    cl->addWidget(m_refreshBtn);
    cl->addWidget(new QLabel(tr("Débit :"), this));
    m_baudCombo = new QComboBox(this);
    m_baudCombo->addItem(tr("Auto"), 0);
    m_baudCombo->addItem("38400", 38400);
    m_baudCombo->addItem("115200", 115200);
    cl->addWidget(m_baudCombo);
    m_connectBtn = new QPushButton(tr("Connecter"), this);
    m_connectBtn->setObjectName("accentBtn");
    cl->addWidget(m_connectBtn);
    root->addWidget(connBox);

    m_statusLabel = new QLabel(tr("Branche l'adaptateur, choisis le port, puis « Connecter »."), this);
    m_statusLabel->setStyleSheet("color:#7c8fa6;");
    root->addWidget(m_statusLabel);

    auto* mid = new QHBoxLayout;

    // ── Datalog live ─────────────────────────────────────────────────────────
    auto* logBox = new QGroupBox(tr("Données live (datalog)"), this);
    auto* ll = new QVBoxLayout(logBox);
    m_pidTable = new QTableWidget(this);
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
    ll->addWidget(m_pidTable);
    auto* lbtn = new QHBoxLayout;
    m_datalogBtn = new QPushButton(tr("Démarrer datalog"), this); m_datalogBtn->setEnabled(false);
    m_csvBtn = new QPushButton(tr("Log CSV…"), this);
    lbtn->addWidget(m_datalogBtn); lbtn->addWidget(m_csvBtn); lbtn->addStretch();
    ll->addLayout(lbtn);
    mid->addWidget(logBox, 1);

    // ── Diagnostic + CAN ─────────────────────────────────────────────────────
    auto* diagBox = new QGroupBox(tr("Diagnostic & CAN"), this);
    auto* dl = new QVBoxLayout(diagBox);
    auto* drow = new QHBoxLayout;
    m_dtcReadBtn = new QPushButton(tr("Lire DTC"), this); m_dtcReadBtn->setEnabled(false);
    m_dtcClearBtn = new QPushButton(tr("Effacer DTC"), this); m_dtcClearBtn->setEnabled(false);
    m_vinBtn = new QPushButton(tr("Lire VIN"), this); m_vinBtn->setEnabled(false);
    drow->addWidget(m_dtcReadBtn); drow->addWidget(m_dtcClearBtn); drow->addWidget(m_vinBtn);
    dl->addLayout(drow);
    m_dtcLabel = new QLabel(tr("Codes défaut : —"), this);
    m_dtcLabel->setWordWrap(true); m_dtcLabel->setStyleSheet("color:#e5e7eb;");
    dl->addWidget(m_dtcLabel);
    m_vinLabel = new QLabel(tr("VIN : —"), this);
    m_vinLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dl->addWidget(m_vinLabel);
    m_canBtn = new QPushButton(tr("Sniffer CAN"), this); m_canBtn->setEnabled(false);
    dl->addWidget(m_canBtn);
    m_canTable = new QTableWidget(this);
    m_canTable->setColumnCount(3);
    m_canTable->setHorizontalHeaderLabels({ tr("ID"), tr("DLC"), tr("Données") });
    m_canTable->verticalHeader()->setVisible(false);
    m_canTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_canTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    dl->addWidget(m_canTable, 1);
    mid->addWidget(diagBox, 1);

    root->addLayout(mid, 1);

    // ── Journal ──────────────────────────────────────────────────────────────
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setFixedHeight(120);
    m_log->setStyleSheet("background:#111827; color:#9ca3af; font-family:monospace;");
    root->addWidget(m_log);

    connect(m_refreshBtn, &QPushButton::clicked, this, &ObdPanel::refreshPorts);
    connect(m_connectBtn, &QPushButton::clicked, this, &ObdPanel::toggleConnect);
    connect(m_datalogBtn, &QPushButton::clicked, this, &ObdPanel::toggleDatalog);
    connect(m_canBtn,     &QPushButton::clicked, this, &ObdPanel::toggleCanSniff);
    connect(m_csvBtn,     &QPushButton::clicked, this, &ObdPanel::toggleCsv);
    connect(m_dtcReadBtn, &QPushButton::clicked, this, &ObdPanel::readDtcs);
    connect(m_dtcClearBtn,&QPushButton::clicked, this, &ObdPanel::clearDtcs);
    connect(m_vinBtn,     &QPushButton::clicked, this, [this]() { m_elm->readVin(); });
}

void ObdPanel::setStatus(const QString& msg, bool error) {
    m_statusLabel->setStyleSheet(error ? "color:#ef4444;" : "color:#7c8fa6;");
    m_statusLabel->setText(msg);
}

void ObdPanel::refreshPorts() {
    m_portCombo->clear();
    for (const auto& p : Elm327::listPorts()) {
        const QString label = (p.likelyElm ? QStringLiteral("★ ") : QString())
                              + p.port + (p.description.isEmpty() ? QString()
                                                                  : QStringLiteral("  (%1)").arg(p.description));
        m_portCombo->addItem(label, p.port);
        if (p.likelyElm) m_portCombo->setCurrentIndex(m_portCombo->count() - 1);
    }
    if (m_portCombo->count() == 0)
        setStatus(tr("Aucun port série détecté. Branche l'adaptateur (et vérifie le groupe « dialout »)."), true);
}

void ObdPanel::toggleConnect() {
    if (m_connected) { m_elm->disconnectPort(); return; }
    if (m_portCombo->currentData().isNull()) { setStatus(tr("Choisis un port."), true); return; }
    const QString port = m_portCombo->currentData().toString();
    const int baud = m_baudCombo->currentData().toInt();
    m_elm->connectPort(port, baud);
}

void ObdPanel::toggleDatalog() {
    if (!m_connected) return;
    if (m_datalog) {
        m_elm->stopPolling();
        m_datalog = false;
        m_datalogBtn->setText(tr("Démarrer datalog"));
    } else {
        QList<std::uint8_t> pids;
        for (const auto& p : ecu::obd2::livePids()) pids.push_back(p.pid);
        m_elm->startPolling(pids, 200);
        m_datalog = true;
        m_datalogBtn->setText(tr("Arrêter datalog"));
    }
}

void ObdPanel::toggleCanSniff() {
    if (!m_connected) return;
    if (m_canSniff) {
        m_elm->stopCanMonitor();
        m_canSniff = false;
        m_canBtn->setText(tr("Sniffer CAN"));
    } else {
        if (m_datalog) toggleDatalog();   // le sniff et le datalog s'excluent
        m_canTable->setRowCount(0); m_canRow.clear();
        m_elm->startCanMonitor();
        m_canSniff = true;
        m_canBtn->setText(tr("Arrêter sniff CAN"));
    }
}

void ObdPanel::toggleCsv() {
    if (m_csv) {
        m_csv->close(); m_csv->deleteLater(); m_csv = nullptr;
        m_csvBtn->setText(tr("Log CSV…"));
        setStatus(tr("Log CSV arrêté."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Enregistrer le log datalog"),
        QStringLiteral("datalog_%1.csv")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        tr("CSV (*.csv)"));
    if (path.isEmpty()) return;
    m_csv = new QFile(path, this);
    if (!m_csv->open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus(tr("Impossible d'écrire %1").arg(path), true);
        m_csv->deleteLater(); m_csv = nullptr; return;
    }
    QTextStream(m_csv) << "time,parametre,valeur,unite\n";
    m_csvBtn->setText(tr("Arrêter CSV"));
    setStatus(tr("Log CSV : %1").arg(path));
}

void ObdPanel::readDtcs()  { if (m_connected) m_elm->readDtcs(); }
void ObdPanel::clearDtcs() { if (m_connected) m_elm->clearDtcs(); }

} // namespace ecu_studio
