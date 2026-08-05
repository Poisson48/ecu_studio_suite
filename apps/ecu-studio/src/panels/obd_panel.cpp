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
#include <QClipboard>
#include <QApplication>

namespace ecu_studio {

namespace {
constexpr int kDtcStored  = 1;
constexpr int kDtcPending = 2;
} // namespace

ObdPanel::ObdPanel(QWidget* parent) : QWidget(parent) {
    m_elm = new Elm327(this);
    buildUi();

    connect(m_elm, &Elm327::connected, this, [this](const QString& v) {
        m_connected = true;
        m_connectBtn->setEnabled(true);
        m_connectBtn->setText(tr("Déconnecter"));
        setStatus(tr("Connecté — %1").arg(v));
        m_datalogBtn->setEnabled(true);
        m_dtcReadBtn->setEnabled(true); m_dtcClearBtn->setEnabled(true);
        m_vinBtn->setEnabled(true); m_canBtn->setEnabled(true);
    });
    connect(m_elm, &Elm327::disconnected, this, [this]() {
        m_connected = false; m_datalog = false; m_canSniff = false;
        m_connectBtn->setEnabled(true);
        m_connectBtn->setText(tr("Connecter"));
        m_datalogBtn->setText(tr("Démarrer datalog"));
        m_canBtn->setText(tr("Sniffer CAN"));
        m_datalogBtn->setEnabled(false);
        m_dtcReadBtn->setEnabled(false); m_dtcClearBtn->setEnabled(false);
        m_vinBtn->setEnabled(false); m_canBtn->setEnabled(false);
        setStatus(tr("Déconnecté."));
    });
    connect(m_elm, &Elm327::errorOccurred, this, [this](const QString& m) {
        m_connectBtn->setEnabled(true);
        setStatus(m, true);
    });
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

    m_dtcTable = new QTableWidget(this);
    m_dtcTable->setColumnCount(3);
    m_dtcTable->setHorizontalHeaderLabels({ tr("Code"), tr("Famille"), tr("Statut") });
    m_dtcTable->verticalHeader()->setVisible(false);
    m_dtcTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dtcTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dtcTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_dtcTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_dtcTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_dtcTable->setMinimumHeight(100);
    dl->addWidget(m_dtcTable);

    auto* dtcBtn = new QHBoxLayout;
    m_dtcCopyBtn = new QPushButton(tr("Copier"), this);
    m_dtcCopyBtn->setEnabled(false);
    m_dtcCopyBtn->setToolTip(tr("Copier les codes dans le presse-papiers"));
    m_dtcExportBtn = new QPushButton(tr("Exporter…"), this);
    m_dtcExportBtn->setEnabled(false);
    m_dtcExportBtn->setToolTip(tr("Exporter les codes en .txt ou .csv"));
    dtcBtn->addWidget(m_dtcCopyBtn); dtcBtn->addWidget(m_dtcExportBtn); dtcBtn->addStretch();
    dl->addLayout(dtcBtn);

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
    connect(m_dtcCopyBtn, &QPushButton::clicked, this, &ObdPanel::copyDtcs);
    connect(m_dtcExportBtn,&QPushButton::clicked, this, &ObdPanel::exportDtcs);
    connect(m_vinBtn,     &QPushButton::clicked, this, [this]() { m_elm->readVin(); });
}

void ObdPanel::setStatus(const QString& msg, bool error) {
    m_statusLabel->setStyleSheet(error ? "color:#ef4444;" : "color:#7c8fa6;");
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
    if ((flags & kDtcStored) && (flags & kDtcPending))
        return tr("mémorisé + en attente");
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
    m_portCombo->clear();
    const auto ports = Elm327::listPorts();
    for (const auto& p : ports) {
        const QString label = (p.likelyElm ? QStringLiteral("★ ") : QString())
                              + p.port + (p.description.isEmpty() ? QString()
                                                                  : QStringLiteral("  (%1)").arg(p.description));
        m_portCombo->addItem(label, p.port);
        if (p.likelyElm) m_portCombo->setCurrentIndex(m_portCombo->count() - 1);
    }
    if (ports.isEmpty()) {
        setStatus(tr("Aucun port série USB détecté. Branche l'adaptateur (et vérifie le groupe « dialout »)."), true);
    } else {
        setStatus(tr("%1 port(s) série — ★ = adaptateur ELM probable. Clique « Connecter ».")
                      .arg(ports.size()));
    }
}

void ObdPanel::toggleConnect() {
    if (m_connected) { m_elm->disconnectPort(); return; }
    if (m_portCombo->currentData().isNull()) { setStatus(tr("Choisis un port."), true); return; }
    m_connectBtn->setEnabled(false);
    setStatus(tr("Connexion…"));
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

void ObdPanel::readDtcs() {
    if (!m_connected) return;
    m_dtcFlags.clear();
    m_dtcTable->setRowCount(0);
    m_dtcCopyBtn->setEnabled(false);
    m_dtcExportBtn->setEnabled(false);
    m_dtcAwaiting = 2;   // mode 03 puis 07 (file ELM)
    setStatus(tr("Lecture DTC modes 03 + 07…"));
    m_elm->readDtcs(false);
    m_elm->readDtcs(true);
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
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
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
        if (csv) {
            ts << code << ',' << dtcFamily(code) << ','
               << dtcStatusText(m_dtcFlags.value(code)) << '\n';
        } else {
            ts << code << '\t' << dtcFamily(code) << '\t'
               << dtcStatusText(m_dtcFlags.value(code)) << '\n';
        }
    }
    setStatus(tr("Export DTC : %1").arg(path));
}

} // namespace ecu_studio
