#include "obd/elm327.h"
#include "ecu/Obd2.hpp"

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QRegularExpression>

namespace ecu_studio {

namespace {
// Séquence d'init OBD : echo off, pas de linefeeds, espaces ON (le parseur OBD2
// attend des octets séparés), headers off (réponses PID propres), protocole auto.
const QStringList kInit = { "ATZ", "ATE0", "ATL0", "ATS1", "ATH0", "ATSP0" };

bool isElmBridge(quint16 vid, quint16 pid) {
    switch (vid) {
        case 0x1A86: return pid == 0x7523 || pid == 0x5523 || pid == 0x55D4; // CH340/341
        case 0x0403: return true;   // FTDI (6001/6010/6011/6014/6015)
        case 0x10C4: return pid == 0xEA60 || pid == 0xEA70 || pid == 0xEA71; // CP210x
        case 0x067B: return pid == 0x2303 || pid == 0x23A3 || pid == 0x23C3; // PL2303
        default:     return false;
    }
}
} // namespace

Elm327::Elm327(QObject* parent) : QObject(parent) {
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, &Elm327::onTimeout);
    m_poll = new QTimer(this);
    connect(m_poll, &QTimer::timeout, this, &Elm327::onPollTick);
}

Elm327::~Elm327() { disconnectPort(); }

QList<SerialPortDesc> Elm327::listPorts() {
    QList<SerialPortDesc> out;
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        SerialPortDesc d;
        d.port = info.portName().startsWith(QLatin1String("/")) ? info.portName()
                                                                : info.systemLocation();
        QString desc = info.description();
        if (info.hasVendorIdentifier() && info.hasProductIdentifier()) {
            d.likelyElm = isElmBridge(info.vendorIdentifier(), info.productIdentifier());
            desc += QStringLiteral(" [%1:%2]")
                        .arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0'))
                        .arg(info.productIdentifier(), 4, 16, QLatin1Char('0'));
        }
        d.description = desc.trimmed();
        out.push_back(d);
    }
    return out;
}

void Elm327::connectPort(const QString& port, int baud) {
    disconnectPort();
    m_port = port;
    m_autoBaud = (baud == 0);
    m_triedHighBaud = false;
    tryOpen(m_autoBaud ? 38400 : baud);
}

void Elm327::tryOpen(int baud) {
    m_baud = baud;
    m_serial = new QSerialPort(this);
    m_serial->setPortName(m_port);
    m_serial->setBaudRate(baud);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);
    if (!m_serial->open(QIODevice::ReadWrite)) {
        const QString err = m_serial->errorString();
        m_serial->deleteLater();
        m_serial = nullptr;
        failConnect(tr("Ouverture %1 impossible : %2 "
                       "(droits ? ajoute ton user au groupe « dialout »)")
                        .arg(m_port, err));
        return;
    }
    connect(m_serial, &QSerialPort::readyRead, this, &Elm327::onReadyRead);
    emit status(tr("Connexion à %1 @ %2 bauds…").arg(m_port).arg(baud));

    // Lance l'init pilotée par prompt.
    m_ready = false; m_canMode = false; m_initStep = 0;
    m_queue.clear(); m_buf.clear(); m_busy = false;
    for (const QString& c : kInit) enqueue(c, Kind::Init);
    sendNext();
}

void Elm327::failConnect(const QString& why) {
    emit errorOccurred(why);
    disconnectPort();
}

void Elm327::disconnectPort() {
    m_poll->stop();
    m_timeout->stop();
    m_pollPids.clear();
    m_queue.clear();
    m_buf.clear();
    m_busy = false; m_canMode = false;
    const bool wasReady = m_ready;
    m_ready = false;
    if (m_serial) {
        if (m_serial->isOpen()) m_serial->close();
        m_serial->deleteLater();
        m_serial = nullptr;
    }
    if (wasReady) emit disconnected();
}

void Elm327::writeRaw(const QByteArray& bytes) {
    if (m_serial && m_serial->isOpen()) m_serial->write(bytes);
}

void Elm327::enqueue(const QString& text, Kind kind, std::uint8_t pid) {
    m_queue.enqueue({ text, kind, pid });
}

void Elm327::sendNext() {
    if (m_canMode || m_busy || m_queue.isEmpty()) return;
    m_current = m_queue.dequeue();
    m_buf.clear();

    if (m_current.kind == Kind::CanStart) {
        // ATMA : flux continu, pas de prompt « > » attendu.
        writeRaw("ATMA\r");
        m_canMode = true;
        emit status(tr("Monitor CAN actif (ATMA)."));
        return;
    }

    m_busy = true;
    writeRaw((m_current.text + "\r").toLatin1());
    const int to = (m_current.text == QLatin1String("ATZ")) ? 3000 : 1500;
    m_timeout->start(to);
}

void Elm327::onReadyRead() {
    if (!m_serial) return;
    m_buf += m_serial->readAll();

    if (m_canMode) {
        // Flux de trames : on découpe sur \r / \n et on parse chaque ligne.
        for (;;) {
            int idx = m_buf.indexOf('\r');
            if (idx < 0) idx = m_buf.indexOf('\n');
            if (idx < 0) break;
            const QByteArray lineB = m_buf.left(idx).trimmed();
            m_buf.remove(0, idx + 1);
            if (lineB.isEmpty()) continue;
            const QString line = QString::fromLatin1(lineB);
            emit rawLine(line);
            const QStringList t = line.split(' ', Qt::SkipEmptyParts);
            if (t.size() < 2) continue;
            bool ok = false;
            const quint32 id = t[0].toUInt(&ok, 16);
            if (!ok) continue;
            QByteArray data;
            for (int i = 1; i < t.size() && data.size() < 8; ++i) {
                if (t[i].size() != 2) continue;
                bool bok = false;
                const uint v = t[i].toUInt(&bok, 16);
                if (bok) data.append(static_cast<char>(v));
            }
            if (!data.isEmpty()) emit canFrame(id, data);
        }
        return;
    }

    // Mode commande : la réponse est complète à l'arrivée du prompt « > ».
    const int p = m_buf.indexOf('>');
    if (p < 0) return;
    m_timeout->stop();
    const QString resp = QString::fromLatin1(m_buf.left(p));
    m_buf.remove(0, p + 1);
    m_busy = false;
    for (const QString& l : resp.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                       Qt::SkipEmptyParts))
        emit rawLine(l.trimmed());
    handleResponse(m_current, resp);
    sendNext();
}

void Elm327::handleResponse(const Cmd& cmd, const QString& resp) {
    switch (cmd.kind) {
        case Kind::Init: processInitStep(resp); break;
        case Kind::Pid: {
            auto r = ecu::obd2::parseResponse(resp, 0x01, cmd.pid);
            if (r.ok) {
                if (auto v = ecu::obd2::interpret(cmd.pid, r.data.data(), r.len))
                    emit pidResult(cmd.pid, *v, ecu::obd2::pidName(cmd.pid),
                                   ecu::obd2::pidUnit(cmd.pid));
                else emit pidUnsupported(cmd.pid);
            } else {
                emit pidUnsupported(cmd.pid);
            }
            break;
        }
        case Kind::Dtc:      emit dtcsReady(ecu::obd2::decodeDtcs(resp)); break;
        case Kind::Vin:      emit vinReady(ecu::obd2::decodeVin(resp));   break;
        case Kind::ClearDtc:
            emit status(resp.contains(QLatin1String("44")) || resp.contains(QLatin1String("OK"))
                            ? tr("Codes défaut effacés.")
                            : tr("Effacement DTC : réponse inattendue."));
            break;
        case Kind::CanStart: case Kind::Raw: break;
    }
}

void Elm327::processInitStep(const QString& resp) {
    ++m_initStep;
    static QString version;
    if (m_initStep == 1) {   // réponse à ATZ
        for (const QString& l : resp.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                           Qt::SkipEmptyParts))
            if (l.contains(QLatin1String("ELM"), Qt::CaseInsensitive)) version = l.trimmed();
    }
    if (m_initStep >= kInit.size()) {
        m_ready = true;
        emit connected(version.isEmpty() ? tr("ELM327") : version);
        emit status(tr("Prêt."));
        if (!m_pollPids.isEmpty() && !m_poll->isActive())
            m_poll->start();   // reprend le polling si demandé avant connexion
    }
}

void Elm327::onTimeout() {
    m_busy = false;
    // Auto-baud : si ATZ ne répond pas, on tente l'autre vitesse usine.
    if (!m_ready && m_current.kind == Kind::Init && m_initStep == 0
            && m_autoBaud && !m_triedHighBaud) {
        m_triedHighBaud = true;
        emit status(tr("Pas de réponse à 38400 — essai à 115200 bauds…"));
        if (m_serial) { m_serial->close(); m_serial->deleteLater(); m_serial = nullptr; }
        tryOpen(115200);
        return;
    }
    if (!m_ready) { failConnect(tr("ELM327 ne répond pas sur %1.").arg(m_port)); return; }
    // En fonctionnement : on abandonne la commande courante et on continue.
    if (m_current.kind == Kind::Pid) emit pidUnsupported(m_current.pid);
    sendNext();
}

// ── API publique ─────────────────────────────────────────────────────────────

void Elm327::queryPid(std::uint8_t pid) {
    if (!m_ready) return;
    enqueue(ecu::obd2::pidRequest(pid), Kind::Pid, pid);
    sendNext();
}

void Elm327::startPolling(const QList<std::uint8_t>& pids, int intervalMs) {
    m_pollPids = pids; m_pollIdx = 0;
    m_poll->setInterval(intervalMs);
    if (m_ready) m_poll->start();
}

void Elm327::stopPolling() { m_poll->stop(); m_pollPids.clear(); }

void Elm327::onPollTick() {
    if (!m_ready || m_canMode || m_pollPids.isEmpty()) return;
    if (m_busy || !m_queue.isEmpty()) return;   // n'empile pas : attend le tour précédent
    const std::uint8_t pid = m_pollPids[m_pollIdx % m_pollPids.size()];
    m_pollIdx = (m_pollIdx + 1) % m_pollPids.size();
    queryPid(pid);
}

void Elm327::readDtcs()  { if (m_ready) { enqueue("03", Kind::Dtc);     sendNext(); } }
void Elm327::clearDtcs() { if (m_ready) { enqueue("04", Kind::ClearDtc);sendNext(); } }
void Elm327::readVin()   { if (m_ready) { enqueue("0902", Kind::Vin);   sendNext(); } }

void Elm327::startCanMonitor() {
    if (!m_ready) return;
    stopPolling();
    enqueue("ATH1", Kind::Raw);     // headers ON pour voir les IDs
    enqueue("ATMA", Kind::CanStart);
    sendNext();
}

void Elm327::stopCanMonitor() {
    if (!m_canMode) return;
    writeRaw("\r");          // tout caractère interrompt ATMA
    m_canMode = false;
    m_buf.clear();
    enqueue("ATH0", Kind::Raw);   // retour mode OBD (headers off)
    m_busy = false;
    sendNext();
}

} // namespace ecu_studio
