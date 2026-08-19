#include "elm/Elm327.hpp"
#include "ecu/Obd2.hpp"
#include "ecu/SecurityAccess.hpp"

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QRegularExpression>
#include <QIODevice>

#if defined(ELM_HAVE_BLUETOOTH)
#  include <QBluetoothSocket>
#  include <QBluetoothAddress>
#  include <QBluetoothServiceInfo>
#  include <QBluetoothUuid>
#endif

#include <algorithm>

namespace elm {

namespace {
const QStringList kInit = { "ATZ", "ATE0", "ATL0", "ATS1", "ATH0", "ATSP0" };

bool isElmBridge(quint16 vid, quint16 pid) {
    switch (vid) {
        case 0x1A86: return pid == 0x7523 || pid == 0x5523 || pid == 0x55D4;
        case 0x0403: return true;
        case 0x10C4: return pid == 0xEA60 || pid == 0xEA70 || pid == 0xEA71;
        case 0x067B: return pid == 0x2303 || pid == 0x23A3 || pid == 0x23C3;
        case 0x0918: return true;
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
#if defined(ELM_HAVE_BLUETOOTH)
    m_btConnectTimeout = new QTimer(this);
    m_btConnectTimeout->setSingleShot(true);
    connect(m_btConnectTimeout, &QTimer::timeout, this, &Elm327::onBtConnectTimeout);
#endif
}

Elm327::~Elm327() { disconnectPort(); }

QList<SerialPortDesc> Elm327::listPorts() {
    QList<SerialPortDesc> out;
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        const QString name = info.portName();
        const bool hasVid = info.hasVendorIdentifier() && info.hasProductIdentifier();
        const bool usbLike =
            name.contains(QLatin1String("ACM"), Qt::CaseInsensitive)
            || name.contains(QLatin1String("USB"), Qt::CaseInsensitive)
            || name.contains(QLatin1String("rfcomm"), Qt::CaseInsensitive)
            || name.startsWith(QLatin1String("cu."), Qt::CaseInsensitive)
            || name.startsWith(QLatin1String("COM"), Qt::CaseInsensitive);
        if (!hasVid && !usbLike) continue;

        SerialPortDesc d;
        d.port = name.startsWith(QLatin1String("/")) ? name : info.systemLocation();
        QString desc = info.description();
        if (hasVid) {
            d.likelyElm = isElmBridge(info.vendorIdentifier(), info.productIdentifier());
            desc += QStringLiteral(" [%1:%2]")
                        .arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0'))
                        .arg(info.productIdentifier(), 4, 16, QLatin1Char('0'));
        }
        d.description = desc.trimmed();
        out.push_back(d);
    }
    std::stable_sort(out.begin(), out.end(), [](const SerialPortDesc& a, const SerialPortDesc& b) {
        return a.likelyElm > b.likelyElm;
    });
    return out;
}

void Elm327::connectPort(const QString& port, int baud) {
    disconnectPort();
    m_port = port;
    m_label = port;
    m_autoBaud = (baud == 0);
    m_triedHighBaud = false;
    m_opening = true;
    tryOpenSerial(m_autoBaud ? 38400 : baud);
}

void Elm327::attachDevice(QIODevice* device, bool ownDevice, const QString& label) {
    disconnectPort();
    if (!device) {
        failConnect(tr("Périphérique invalide"));
        return;
    }
    m_io = device;
    m_ownIo = ownDevice;
    m_label = label.isEmpty() ? QStringLiteral("device") : label;
    m_port = m_label;
    m_opening = true;
    if (!m_io->isOpen() && !m_io->open(QIODevice::ReadWrite)) {
        failConnect(tr("Ouverture impossible : %1").arg(m_io->errorString()));
        return;
    }
    connect(m_io, &QIODevice::readyRead, this, &Elm327::onReadyRead);
    beginInit();
}

#if defined(ELM_HAVE_BLUETOOTH)
void Elm327::connectBluetooth(const QString& address) {
    disconnectPort();
    const QBluetoothAddress addr(address);
    if (addr.isNull()) {
        failConnect(tr("Adresse Bluetooth invalide"));
        return;
    }
    m_btAddress = address;
    m_btAttempt = 0;
    m_btTransport = true;
    m_atzRetried = false;
    m_opening = true;
    m_label = address;
    m_port = address;
    tryNextBtAttempt();
}

void Elm327::tryNextBtAttempt() {
    if (m_btAttempt > 2) {
        failConnect(tr("Bluetooth : impossible de joindre %1\n"
                       "(SPP / canaux 1–2).\n"
                       "Appaire le module (PIN 1234 ou 0000) puis réessaie.")
                        .arg(m_btAddress));
        return;
    }
    startBtSocket();
}

void Elm327::startBtSocket() {
    // Ferme l'essai précédent sans toucher m_connectEpoch / m_opening.
    if (m_bt) {
        QObject::disconnect(m_bt, nullptr, this, nullptr);
        if (m_bt->isOpen()) m_bt->close();
        m_bt->deleteLater();
        m_bt = nullptr;
    }
    m_io = nullptr;

    m_bt = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);
    connect(m_bt, &QBluetoothSocket::connected, this, &Elm327::onBtConnected);
    connect(m_bt, &QBluetoothSocket::errorOccurred, this, &Elm327::onBtError);
    connect(m_bt, &QBluetoothSocket::disconnected, this, &Elm327::onBtLinkLost);

    const QBluetoothAddress addr(m_btAddress);
    if (m_btAttempt == 0) {
        emit status(tr("Bluetooth → %1 (SPP UUID)…").arg(m_btAddress));
        m_bt->connectToService(
            addr, QBluetoothUuid(QBluetoothUuid::ServiceClassUuid::SerialPort));
    } else {
        const quint16 ch = (m_btAttempt == 1) ? quint16(1) : quint16(2);
        emit status(tr("Bluetooth → %1 (RFCOMM %2)…").arg(m_btAddress).arg(ch));
        m_bt->connectToService(addr, ch);
    }
    m_btConnectTimeout->start(18000);
}

void Elm327::onBtConnected() {
    if (m_btConnectTimeout) m_btConnectTimeout->stop();
    m_io = m_bt;
    m_ownIo = false;
    connect(m_bt, &QIODevice::readyRead, this, &Elm327::onReadyRead);
    // Clones chinois : jettent souvent les premiers octets juste après RFCOMM up.
    const int epoch = m_connectEpoch;
    QTimer::singleShot(400, this, [this, epoch]() {
        if (epoch != m_connectEpoch || !m_bt || !m_bt->isOpen()) return;
        beginInit();
    });
}

void Elm327::onBtError() {
    if (!m_bt) return;
    // Perte de lien une fois connecté (moteur coupé, dongle hors portée…).
    if (!m_opening) {
        if (m_ready)
            onBtLinkLost();
        return;
    }
    if (m_btConnectTimeout) m_btConnectTimeout->stop();
    const QString err = m_bt->errorString();
    ++m_btAttempt;
    if (m_btAttempt <= 2) {
        emit status(tr("BT échec (%1) — nouvel essai…").arg(err));
        QTimer::singleShot(250, this, [this]() {
            if (m_opening) tryNextBtAttempt();
        });
        return;
    }
    failConnect(tr("Bluetooth : %1").arg(err));
}

void Elm327::onBtLinkLost() {
    if (m_opening) return;          // phase connexion : géré par onBtError
    if (!m_btTransport) return;
    if (!m_ready) return;           // déjà en cours de fermeture
    emit errorOccurred(tr("Connexion Bluetooth perdue avec le module.\n"
                          "Vérifie l'alimentation du dongle et la portée."));
    disconnectPort();
}

void Elm327::onBtConnectTimeout() {
    if (!m_opening || !m_btTransport) return;
    ++m_btAttempt;
    if (m_btAttempt <= 2) {
        emit status(tr("BT timeout — nouvel essai…"));
        tryNextBtAttempt();
        return;
    }
    failConnect(tr("Bluetooth : délai dépassé pour %1.\n"
                   "Module allumé (contact ON) et appairé ?")
                    .arg(m_btAddress));
}
#endif

void Elm327::tryOpenSerial(int baud) {
    m_baud = baud;
    closeTransport();
    m_serial = new QSerialPort(this);
    m_serial->setPortName(m_port);
    m_serial->setBaudRate(baud);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);
    if (!m_serial->open(QIODevice::ReadWrite)) {
        const QString err = m_serial->errorString();
        closeTransport();
        QString hint = tr("(droits ? groupe « dialout »)");
        if (err.contains(QLatin1String("busy"), Qt::CaseInsensitive))
            hint = tr("(port déjà ouvert)");
        failConnect(tr("Ouverture %1 impossible : %2 %3").arg(m_port, err, hint));
        return;
    }
    if (isCdcAcmPort()) {
        m_serial->setDataTerminalReady(true);
        m_serial->setRequestToSend(true);
    }
    m_serial->clear(QSerialPort::AllDirections);
    m_io = m_serial;
    m_ownIo = false;
    connect(m_serial, &QSerialPort::readyRead, this, &Elm327::onReadyRead);
    emit status(tr("Connexion à %1 @ %2 bauds…").arg(m_port).arg(baud));
    beginInit();
}

void Elm327::beginInit() {
    m_ready = false; m_canMode = false; m_initStep = 0; m_elmVersion.clear();
    m_queue.clear(); m_buf.clear(); m_busy = false;
#if defined(ELM_HAVE_BLUETOOTH)
    m_atzRetried = false;
#endif
    for (const QString& c : kInit) enqueue(c, Kind::Init);
    const int epoch = ++m_connectEpoch;
    int delayMs = isCdcAcmPort() ? 250 : 0;
#if defined(ELM_HAVE_BLUETOOTH)
    if (m_btTransport) delayMs = 0; // déjà attendu dans onBtConnected
#endif
    QTimer::singleShot(delayMs, this, [this, epoch]() {
        if (epoch != m_connectEpoch || !m_io || !m_io->isOpen()) return;
        sendNext();
    });
}

void Elm327::failConnect(const QString& why) {
    m_opening = false;
    emit errorOccurred(why);
    disconnectPort();
}

void Elm327::closeTransport() {
    if (m_serial) {
        QObject::disconnect(m_serial, nullptr, this, nullptr);
        if (m_serial->isOpen()) m_serial->close();
        m_serial->deleteLater();
        m_serial = nullptr;
    }
#if defined(ELM_HAVE_BLUETOOTH)
    if (m_bt) {
        QObject::disconnect(m_bt, nullptr, this, nullptr);
        if (m_bt->isOpen()) m_bt->close();
        m_bt->deleteLater();
        m_bt = nullptr;
    }
#endif
    if (m_io && m_ownIo) {
        QObject::disconnect(m_io, nullptr, this, nullptr);
        if (m_io->isOpen()) m_io->close();
        m_io->deleteLater();
    } else if (m_io) {
        QObject::disconnect(m_io, nullptr, this, nullptr);
    }
    m_io = nullptr;
    m_ownIo = false;
}

void Elm327::disconnectPort() {
    ++m_connectEpoch;
    m_poll->stop();
    m_timeout->stop();
#if defined(ELM_HAVE_BLUETOOTH)
    if (m_btConnectTimeout) m_btConnectTimeout->stop();
    m_btTransport = false;
    m_btAddress.clear();
    m_btAttempt = 0;
#endif
    m_pollPids.clear();
    m_queue.clear();
    m_buf.clear();
    m_busy = false; m_canMode = false; m_opening = false;
    const bool wasReady = m_ready;
    m_ready = false;
    closeTransport();
    if (wasReady) emit disconnected();
}

void Elm327::writeRaw(const QByteArray& bytes) {
    if (m_io && m_io->isOpen()) m_io->write(bytes);
}

void Elm327::enqueue(const QString& text, Kind kind, std::uint8_t pid) {
    m_queue.enqueue({ text, kind, pid });
}

void Elm327::sendNext() {
    if (m_canMode || m_busy || m_queue.isEmpty()) return;
    m_current = m_queue.dequeue();
    m_buf.clear();

    if (m_current.kind == Kind::CanStart) {
        writeRaw("ATMA\r");
        m_canMode = true;
        emit status(tr("Monitor CAN actif (ATMA)."));
        return;
    }

    m_busy = true;
    writeRaw((m_current.text + "\r").toLatin1());
    int to = (m_current.text == QLatin1String("ATZ")) ? 5000 : 1500;
#if defined(ELM_HAVE_BLUETOOTH)
    if (m_btTransport) {
        to = (m_current.text == QLatin1String("ATZ")) ? 8000 : 3000;
    }
#endif
    m_timeout->start(to);
}

void Elm327::onReadyRead() {
    if (!m_io) return;
    m_buf += m_io->readAll();

    if (m_canMode) {
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
        case Kind::PidSupport: {
            auto r = ecu::obd2::parseResponse(resp, 0x01, cmd.pid);
            if (r.ok) {
                const auto chunk = ecu::obd2::decodeSupportedPidBitmap(
                    cmd.pid, r.data.data(), r.len, nullptr);
                for (std::uint8_t p : chunk) {
                    if (!m_supportedAccum.contains(p))
                        m_supportedAccum.append(p);
                }
            }
            if (m_supportRemaining > 0)
                --m_supportRemaining;
            if (m_supportRemaining <= 0) {
                std::sort(m_supportedAccum.begin(), m_supportedAccum.end());
                emit supportedPidsReady(m_supportedAccum);
                emit status(tr("PID supportés : %1").arg(m_supportedAccum.size()));
            }
            break;
        }
        case Kind::Dtc: {
            const bool pending = (cmd.pid == 0x07);
            emit dtcsReady(ecu::obd2::decodeDtcs(resp, cmd.pid), pending);
            break;
        }
        case Kind::Vin:      emit vinReady(ecu::obd2::decodeVin(resp));   break;
        case Kind::FreezeFrame: {
            auto r = ecu::obd2::parseResponse(resp, 0x02, cmd.pid);
            if (r.ok) {
                if (auto v = ecu::obd2::interpret(cmd.pid, r.data.data(), r.len))
                    emit freezeFrameResult(cmd.pid, *v, ecu::obd2::pidName(cmd.pid),
                                           ecu::obd2::pidUnit(cmd.pid));
                else emit pidUnsupported(cmd.pid);
            } else {
                emit pidUnsupported(cmd.pid);
            }
            break;
        }
        case Kind::ClearDtc:
            emit status(resp.contains(QLatin1String("44")) || resp.contains(QLatin1String("OK"))
                            ? tr("Codes défaut effacés.")
                            : tr("Effacement DTC : réponse inattendue."));
            break;
        case Kind::CanStart:
            break;
        case Kind::Raw:
            emit rawResponse(cmd.text, resp.trimmed());
            break;
    }
}

void Elm327::processInitStep(const QString& resp) {
    ++m_initStep;
    if (m_initStep == 1) {
        for (const QString& l : resp.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                           Qt::SkipEmptyParts))
            if (l.contains(QLatin1String("ELM"), Qt::CaseInsensitive))
                m_elmVersion = l.trimmed();
    }
    if (m_initStep >= kInit.size()) {
        m_ready = true;
        m_opening = false;
        emit connected(m_elmVersion.isEmpty() ? tr("ELM327") : m_elmVersion);
        emit status(tr("Prêt."));
        if (!m_pollPids.isEmpty() && !m_poll->isActive())
            m_poll->start();
    }
}

void Elm327::onTimeout() {
    m_busy = false;
    if (!m_ready && m_current.kind == Kind::Init && m_initStep == 0
            && m_autoBaud && !m_triedHighBaud && !isCdcAcmPort() && m_serial) {
        m_triedHighBaud = true;
        emit status(tr("Pas de réponse à 38400 — essai à 115200 bauds…"));
        tryOpenSerial(115200);
        return;
    }
#if defined(ELM_HAVE_BLUETOOTH)
    // Un ATZ raté sur clone BT : un seul retry avant d'abandonner.
    if (!m_ready && m_btTransport && m_current.kind == Kind::Init
            && m_initStep == 0 && !m_atzRetried
            && m_current.text == QLatin1String("ATZ")) {
        m_atzRetried = true;
        emit status(tr("Pas de réponse ATZ — nouvel essai…"));
        QQueue<Cmd> rest = m_queue;
        m_queue.clear();
        m_queue.enqueue({ QStringLiteral("ATZ"), Kind::Init, 0 });
        while (!rest.isEmpty())
            m_queue.enqueue(rest.dequeue());
        QTimer::singleShot(300, this, [this]() { sendNext(); });
        return;
    }
#endif
    if (!m_ready) {
        failConnect(tr("ELM327 ne répond pas sur %1.\n"
                       "Contact ON + adaptateur OBD alimenté.")
                        .arg(m_label));
        return;
    }
    if (m_current.kind == Kind::Pid) emit pidUnsupported(m_current.pid);
    if (m_current.kind == Kind::Raw)
        emit rawResponse(m_current.text, tr("(timeout — pas de réponse)"));
    sendNext();
}

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

void Elm327::probeSupportedPids() {
    if (!m_ready) return;
    stopPolling();
    m_supportedAccum.clear();
    m_supportRemaining = 0;
    // Blocs SAE J1979 : 0x00→01..20, 0x20→21..40, 0x40→41..60, 0x60→61..80.
    for (std::uint8_t base : { std::uint8_t{0x00}, std::uint8_t{0x20},
                               std::uint8_t{0x40}, std::uint8_t{0x60} }) {
        enqueue(ecu::obd2::pidRequest(base), Kind::PidSupport, base);
        ++m_supportRemaining;
    }
    emit status(tr("Découverte des PID supportés…"));
    sendNext();
}

void Elm327::onPollTick() {
    if (!m_ready || m_canMode || m_pollPids.isEmpty()) return;
    if (m_busy || !m_queue.isEmpty()) return;
    const std::uint8_t pid = m_pollPids[m_pollIdx % m_pollPids.size()];
    m_pollIdx = (m_pollIdx + 1) % m_pollPids.size();
    queryPid(pid);
}

void Elm327::readDtcs(bool pending) {
    if (!m_ready) return;
    const std::uint8_t mode = pending ? 0x07 : 0x03;
    enqueue(pending ? QStringLiteral("07") : QStringLiteral("03"), Kind::Dtc, mode);
    sendNext();
}
void Elm327::clearDtcs() { if (m_ready) { enqueue("04", Kind::ClearDtc); sendNext(); } }
void Elm327::readVin()   { if (m_ready) { enqueue("0902", Kind::Vin); sendNext(); } }

void Elm327::readFreezeFrame(const QList<std::uint8_t>& pids) {
    if (!m_ready) return;
    QList<std::uint8_t> list = pids;
    if (list.isEmpty()) {
        for (const auto& p : ecu::obd2::freezeFramePids()) list.push_back(p.pid);
    }
    for (std::uint8_t pid : list)
        enqueue(ecu::obd2::freezeFrameRequest(pid), Kind::FreezeFrame, pid);
    sendNext();
}

void Elm327::startCanMonitor() {
    if (!m_ready) return;
    stopPolling();
    enqueue("ATH1", Kind::Raw);
    enqueue("ATMA", Kind::CanStart);
    sendNext();
}

void Elm327::stopCanMonitor() {
    if (!m_canMode) return;
    writeRaw("\r");
    m_canMode = false;
    m_buf.clear();
    enqueue("ATH0", Kind::Raw);
    m_busy = false;
    sendNext();
}

void Elm327::sendRawCommand(const QString& command) {
    if (!m_ready) return;
    QString cmd = command.trimmed();
    cmd.remove(QLatin1Char('\r'));
    cmd.remove(QLatin1Char('\n'));
    if (cmd.isEmpty()) return;
    enqueue(cmd, Kind::Raw);
    sendNext();
}

void Elm327::sendSecurityAccessRequest(const QString& algoName,
                                       int level,
                                       quint16 ecuKey,
                                       bool isKwp)
{
    if (!m_ready) {
        emit securityAccessResult(false, {}, tr("ELM327 non connect\u00e9"));
        return;
    }

    const auto algoOpt = ecu::SecurityAccess::fromName(algoName.toStdString());
    if (!algoOpt) {
        emit securityAccessResult(false, {}, tr("Algorithme inconnu : %1").arg(algoName));
        return;
    }
    const ecu::SecurityAccess::Algo algo = *algoOpt;

    QString seedRequest;
    if (isKwp) {
        const uint8_t subSeed = (level == 1)
            ? ecu::SecurityAccess::KWP_SEED_DOWNLOAD
            : ecu::SecurityAccess::KWP_SEED_CONFIG;
        seedRequest = QStringLiteral("27%1").arg(subSeed, 2, 16, QLatin1Char('0')).toUpper();
    } else {
        const uint8_t subSeed = (uint8_t)((level - 1) * 2 + 1);
        seedRequest = QStringLiteral("27%1").arg(subSeed, 2, 16, QLatin1Char('0')).toUpper();
    }

    auto* conn = new QMetaObject::Connection;
    *conn = connect(this, &Elm327::rawResponse, this,
        [this, conn, algo, ecuKey, level, isKwp](
            const QString& cmd, const QString& resp)
        {
            if (!cmd.startsWith(QLatin1String("27"), Qt::CaseInsensitive))
                return;

            disconnect(*conn);
            delete conn;

            const QString flat = QString(resp).remove(QLatin1Char(' ')).remove(QLatin1Char('\n'))
                                              .remove(QLatin1Char('\r')).toUpper();
            const int idx = flat.indexOf(QLatin1String("67"));
            if (idx < 0 || flat.size() < idx + 12) {
                emit securityAccessResult(false, {}, tr("R\u00e9ponse seed invalide : %1").arg(resp.trimmed()));
                return;
            }

            const QString seedHex = flat.mid(idx + 4, 8);
            bool ok = false;
            const quint32 seed = seedHex.toUInt(&ok, 16);
            if (!ok) {
                emit securityAccessResult(false, {}, tr("Seed non parseable : %1").arg(seedHex));
                return;
            }

            const auto keyOpt = ecu::SecurityAccess::compute(algo, seed, ecuKey);
            if (!keyOpt) {
                emit securityAccessResult(false, {}, tr("Calcul key \u00e9chou\u00e9"));
                return;
            }

            const QString keyHex = QString::number(*keyOpt, 16).toUpper()
                                       .rightJustified(8, QLatin1Char('0'));

            QString keyFrame;
            if (isKwp) {
                const uint8_t subKey = (level == 1)
                    ? ecu::SecurityAccess::KWP_KEY_DOWNLOAD
                    : ecu::SecurityAccess::KWP_KEY_CONFIG;
                keyFrame = QStringLiteral("27%1%2")
                    .arg(subKey, 2, 16, QLatin1Char('0')).arg(keyHex).toUpper();
            } else {
                const uint8_t subKey = (uint8_t)((level - 1) * 2 + 2);
                keyFrame = QStringLiteral("27%1%2")
                    .arg(subKey, 2, 16, QLatin1Char('0')).arg(keyHex).toUpper();
            }

            auto* conn2 = new QMetaObject::Connection;
            *conn2 = connect(this, &Elm327::rawResponse, this,
                [this, conn2, keyHex](const QString&, const QString& r2) {
                    disconnect(*conn2);
                    delete conn2;
                    const bool success = r2.contains(QLatin1String("67"), Qt::CaseInsensitive)
                                      && !r2.contains(QLatin1String("7F"), Qt::CaseInsensitive);
                    emit securityAccessResult(success, keyHex,
                        success ? tr("D\u00e9verrouill\u00e9")
                                : tr("Rejet\u00e9 par ECU : %1").arg(r2.trimmed()));
                });

            sendRawCommand(keyFrame);
        });

    sendRawCommand(seedRequest);
}

void Elm327::readEcuZone(quint8 zoneId)
{
    if (!m_ready) return;
    sendRawCommand(QStringLiteral("21%1").arg(zoneId, 2, 16, QLatin1Char('0')).toUpper());
}

void Elm327::writeEcuZone(quint8 zoneId, const QByteArray& data)
{
    if (!m_ready) return;
    QString cmd = QStringLiteral("3B%1").arg(zoneId, 2, 16, QLatin1Char('0')).toUpper();
    for (const uchar b : data)
        cmd += QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
    sendRawCommand(cmd);
}

} // namespace elm
