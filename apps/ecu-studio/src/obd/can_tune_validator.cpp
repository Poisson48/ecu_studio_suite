#include "obd/can_tune_validator.h"

#include "hub/sub_program_registry.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLabel>
#include <QProcess>
#include <QTcpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>

#include <cmath>

namespace ecu_studio {

namespace {
constexpr quint16 kPortMin = 49300;
constexpr quint16 kPortMax = 49999;
constexpr int kSampleWindowMs = 400;
} // namespace

CanTuneValidator::CanTuneValidator(QWidget* parent) : QWidget(parent) {
    buildUi();
    refreshInterfaces();
}

CanTuneValidator::~CanTuneValidator() {
    stopMonitoring(false, tr("Fermé."));
}

void CanTuneValidator::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    auto* form = new QFormLayout;
    m_ifaceCombo = new QComboBox(this);
    auto* ifaceRow = new QHBoxLayout;
    ifaceRow->addWidget(m_ifaceCombo, 1);
    auto* refreshBtn = new QPushButton(tr("↻"), this);
    refreshBtn->setFixedWidth(28);
    connect(refreshBtn, &QPushButton::clicked, this, &CanTuneValidator::refreshInterfaces);
    ifaceRow->addWidget(refreshBtn);
    form->addRow(tr("Interface CAN :"), ifaceRow);

    m_signalEdit = new QLineEdit(this);
    m_signalEdit->setPlaceholderText(tr("Nom signal DBC (ex. BoostPressure)"));
    form->addRow(tr("Signal :"), m_signalEdit);

    m_targetSpin = new QDoubleSpinBox(this);
    m_targetSpin->setRange(-1e6, 1e6);
    m_targetSpin->setDecimals(2);
    m_targetSpin->setValue(1800.0);
    form->addRow(tr("Cible :"), m_targetSpin);

    m_toleranceSpin = new QDoubleSpinBox(this);
    m_toleranceSpin->setRange(0, 1e6);
    m_toleranceSpin->setDecimals(1);
    m_toleranceSpin->setValue(50.0);
    form->addRow(tr("Tolérance ± :"), m_toleranceSpin);

    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(200, 5000);
    m_intervalSpin->setValue(750);
    m_intervalSpin->setSuffix(tr(" ms"));
    form->addRow(tr("Intervalle :"), m_intervalSpin);

    root->addLayout(form);

    m_liveLabel = new QLabel(tr("Mesuré : —"), this);
    m_liveLabel->setStyleSheet("font-size:14px; font-weight:bold; color:#60a5fa;");
    root->addWidget(m_liveLabel);

    m_startBtn = new QPushButton(tr("Démarrer validation CAN"), this);
    m_startBtn->setObjectName("accentBtn");
    connect(m_startBtn, &QPushButton::clicked, this, &CanTuneValidator::onStartStop);
    root->addWidget(m_startBtn);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color:#7c8fa6; font-size:11px;");
    root->addWidget(m_statusLabel);

    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(200);
    m_logView->setFixedHeight(80);
    m_logView->setStyleSheet("background:#111827; color:#9ca3af; font-family:monospace; font-size:10px;");
    root->addWidget(m_logView);
}

void CanTuneValidator::setTargetSignal(const QString& signal, const QString& unit) {
    m_signalEdit->setText(signal);
    if (!unit.isEmpty())
        m_targetSpin->setSuffix(QStringLiteral(" %1").arg(unit));
}

QStringList CanTuneValidator::detectInterfaces() {
    QStringList out;
    QDir netDir(QStringLiteral("/sys/class/net"));
    if (!netDir.exists()) return out;
    for (const QString& name : netDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (name.startsWith(QStringLiteral("can"))
            || name.startsWith(QStringLiteral("vcan"))
            || name.startsWith(QStringLiteral("slcan")))
            out << name;
    }
    out.sort();
    return out;
}

void CanTuneValidator::refreshInterfaces() {
    const QString cur = m_ifaceCombo->currentText();
    m_ifaceCombo->clear();
    const QStringList ifaces = detectInterfaces();
    m_ifaceCombo->addItems(ifaces);
    if (!cur.isEmpty()) {
        const int idx = m_ifaceCombo->findText(cur);
        if (idx >= 0) m_ifaceCombo->setCurrentIndex(idx);
    } else if (ifaces.isEmpty()) {
        m_ifaceCombo->setCurrentText(QStringLiteral("vcan0"));
    }
}

void CanTuneValidator::log(const QString& msg, bool error) {
    const QString line = QStringLiteral("[%1] %2")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                                  msg);
    if (error)
        m_logView->appendHtml(QStringLiteral("<span style='color:#ef4444;'>%1</span>")
                                  .arg(line.toHtmlEscaped()));
    else
        m_logView->appendPlainText(line);
}

void CanTuneValidator::onStartStop() {
    if (m_running) stopMonitoring(false, tr("Arrêté par l'utilisateur."));
    else startMonitoring();
}

void CanTuneValidator::startMonitoring() {
    const QString iface = m_ifaceCombo->currentText().trimmed();
    if (iface.isEmpty()) { log(tr("Interface CAN requise."), true); return; }
    if (m_signalEdit->text().trimmed().isEmpty()) { log(tr("Signal requis."), true); return; }

    const QString exe = SubProgramRegistry::resolveExec(QStringLiteral("socketspy-mcp"));
    if (exe.isEmpty()) {
        log(tr("socketspy-mcp introuvable."), true);
        return;
    }

    m_rxBuffer.clear();
    m_pending.clear();
    m_initialized = false;
    m_running = true;
    m_nextId = 1;
    m_startBtn->setText(tr("Arrêter validation CAN"));
    m_statusLabel->setText(tr("Démarrage…"));

    m_port = static_cast<quint16>(kPortMin + QRandomGenerator::global()->bounded(kPortMax - kPortMin));

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::started, this, &CanTuneValidator::onProcStarted);
    connect(m_proc, &QProcess::errorOccurred, this, &CanTuneValidator::onProcErrorOccurred);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
                if (m_running) stopMonitoring(false, tr("MCP terminé (code %1).").arg(code));
            });
    m_proc->setProgram(exe);
    m_proc->setArguments({QStringLiteral("--tcp"), QString::number(m_port)});
    m_proc->start();
}

void CanTuneValidator::stopMonitoring(bool ok, const QString& reason) {
    Q_UNUSED(ok);
    m_running = false;
    if (m_pollTimer) m_pollTimer->stop();
    if (m_socket) { m_socket->abort(); m_socket->deleteLater(); m_socket = nullptr; }
    if (m_proc) {
        if (m_proc->state() != QProcess::NotRunning) {
            m_proc->terminate();
            if (!m_proc->waitForFinished(800)) { m_proc->kill(); m_proc->waitForFinished(300); }
        }
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    m_startBtn->setText(tr("Démarrer validation CAN"));
    m_statusLabel->setText(reason);
    log(reason, !ok);
}

void CanTuneValidator::onProcStarted() {
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &CanTuneValidator::onSocketConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &CanTuneValidator::onSocketReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &CanTuneValidator::onSocketError);
    QTimer::singleShot(150, this, [this]() {
        if (m_socket && m_running)
            m_socket->connectToHost(QHostAddress::LocalHost, m_port);
    });
}

void CanTuneValidator::onProcErrorOccurred(int err) {
    if (!m_running) return;
    if (static_cast<QProcess::ProcessError>(err) == QProcess::FailedToStart)
        stopMonitoring(false, tr("Impossible de lancer MCP."));
}

void CanTuneValidator::onSocketConnected() {
    QJsonObject initParams;
    initParams.insert(QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05"));
    QJsonObject clientInfo;
    clientInfo.insert(QStringLiteral("name"), QStringLiteral("ecu-studio-can-val"));
    clientInfo.insert(QStringLiteral("version"), QStringLiteral("1.0"));
    initParams.insert(QStringLiteral("clientInfo"), clientInfo);
    const int id = sendRequest(QStringLiteral("initialize"), initParams);
    m_pending.insert(id, QStringLiteral("initialize"));
}

void CanTuneValidator::onSocketError() {
    if (!m_running || !m_socket) return;
    if (!m_initialized) {
        QTimer::singleShot(250, this, [this]() {
            if (m_socket && m_running && m_socket->state() == QAbstractSocket::UnconnectedState)
                m_socket->connectToHost(QHostAddress::LocalHost, m_port);
        });
        return;
    }
    stopMonitoring(false, m_socket->errorString());
}

void CanTuneValidator::onSocketReadyRead() {
    if (!m_socket) return;
    m_rxBuffer += m_socket->readAll();
    int nl;
    while ((nl = m_rxBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_rxBuffer.left(nl);
        m_rxBuffer.remove(0, nl + 1);
        if (line.trimmed().isEmpty()) continue;
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
        if (perr.error == QJsonParseError::NoError && doc.isObject())
            handleResponse(doc.object());
    }
}

int CanTuneValidator::sendRequest(const QString& method, const QJsonObject& params) {
    const int id = m_nextId++;
    QJsonObject req;
    req.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    req.insert(QStringLiteral("id"), id);
    req.insert(QStringLiteral("method"), method);
    if (!params.isEmpty()) req.insert(QStringLiteral("params"), params);
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
    return id;
}

int CanTuneValidator::callTool(const QString& toolName, const QJsonObject& arguments) {
    QJsonObject params;
    params.insert(QStringLiteral("name"), toolName);
    params.insert(QStringLiteral("arguments"), arguments);
    return sendRequest(QStringLiteral("tools/call"), params);
}

QJsonObject CanTuneValidator::extractToolPayload(const QJsonObject& result) {
    const QJsonArray content = result.value(QStringLiteral("content")).toArray();
    if (content.isEmpty()) return {};
    const QString text = content.first().toObject().value(QStringLiteral("text")).toString();
    if (text.isEmpty()) return {};
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &perr);
    return (perr.error == QJsonParseError::NoError && doc.isObject()) ? doc.object() : QJsonObject{};
}

void CanTuneValidator::handleResponse(const QJsonObject& response) {
    if (response.contains(QStringLiteral("error"))) {
        stopMonitoring(false, response.value(QStringLiteral("error")).toObject()
                              .value(QStringLiteral("message")).toString());
        return;
    }
    const int id = response.value(QStringLiteral("id")).toInt(-1);
    const QString kind = m_pending.take(id);
    const QJsonObject result = response.value(QStringLiteral("result")).toObject();

    if (kind == QStringLiteral("initialize")) {
        m_initialized = true;
        if (!m_pollTimer) {
            m_pollTimer = new QTimer(this);
            connect(m_pollTimer, &QTimer::timeout, this, &CanTuneValidator::onPollTick);
        }
        requestSample();
        m_pollTimer->start(m_intervalSpin->value());
        m_statusLabel->setText(tr("Validation CAN active."));
        return;
    }

    if (kind == QStringLiteral("can_monitor")) {
        const QJsonObject payload = extractToolPayload(result);
        const QString wanted = m_signalEdit->text().trimmed();
        const double target = m_targetSpin->value();
        const double tol = m_toleranceSpin->value();

        for (const QJsonValue& fv : payload.value(QStringLiteral("frames")).toArray()) {
            for (const QJsonValue& sv : fv.toObject().value(QStringLiteral("signals")).toArray()) {
                const QJsonObject sig = sv.toObject();
                if (sig.value(QStringLiteral("name")).toString().compare(wanted, Qt::CaseInsensitive) != 0)
                    continue;
                const double measured = sig.value(QStringLiteral("value")).toDouble();
                const double delta = measured - target;
                const QString col = std::abs(delta) <= tol ? QStringLiteral("#22c55e")
                                                         : QStringLiteral("#ef4444");
                m_liveLabel->setText(tr("Mesuré : %1 (Δ %2)")
                                         .arg(measured, 0, 'f', 1)
                                         .arg(delta, 0, 'f', 1));
                m_liveLabel->setStyleSheet(QStringLiteral("font-size:14px; font-weight:bold; color:%1;")
                                               .arg(col));
                emit measuredValueChanged(measured, target, delta);
                return;
            }
        }
    }
}

void CanTuneValidator::onPollTick() {
    if (m_running && m_initialized) requestSample();
}

void CanTuneValidator::requestSample() {
    QJsonObject args;
    args.insert(QStringLiteral("iface"), m_ifaceCombo->currentText().trimmed());
    args.insert(QStringLiteral("duration_ms"), kSampleWindowMs);
    args.insert(QStringLiteral("decoded"), true);
    const int id = callTool(QStringLiteral("can_monitor"), args);
    m_pending.insert(id, QStringLiteral("can_monitor"));
}

} // namespace ecu_studio
