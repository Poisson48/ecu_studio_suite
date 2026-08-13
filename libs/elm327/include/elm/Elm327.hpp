#pragma once
//
// Elm327 — driver ELM327 partagé (USB série + Bluetooth RFCOMM).
// Transport abstrait via QIODevice* pour Android / desktop.
//
#include <QObject>
#include <QByteArray>
#include <QList>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <cstdint>

class QIODevice;
class QSerialPort;
class QTimer;

#if defined(ELM_HAVE_BLUETOOTH)
class QBluetoothSocket;
class QBluetoothAddress;
#endif

namespace elm {

struct SerialPortDesc {
    QString port;
    QString description;
    bool    likelyElm = false;
};

struct BluetoothDeviceDesc {
    QString address;   // XX:XX:XX:XX:XX:XX
    QString name;
};

class Elm327 : public QObject {
    Q_OBJECT
public:
    explicit Elm327(QObject* parent = nullptr);
    ~Elm327() override;

    static QList<SerialPortDesc> listPorts();

#if defined(ELM_HAVE_BLUETOOTH)
    // BT classique SPP (modules ELM327 bleus chinois) :
    // UUID SerialPort → canal RFCOMM 1 → canal 2.
    void connectBluetooth(const QString& address);
    bool isBluetoothTransport() const { return m_btTransport; }
#endif

    void connectPort(const QString& port, int baud = 0);
    // Prend ownership si ownDevice=true.
    void attachDevice(QIODevice* device, bool ownDevice, const QString& label = {});
    void disconnectPort();
    bool isReady() const { return m_ready; }
    QString transportLabel() const { return m_label; }

    void queryPid(std::uint8_t pid);
    void startPolling(const QList<std::uint8_t>& pids, int intervalMs = 250);
    void stopPolling();
    /** Interroge 01 00 / 20 / 40 / 60 puis émet supportedPidsReady. */
    void probeSupportedPids();
    void readDtcs(bool pending = false);
    void clearDtcs();
    void readVin();
    void readFreezeFrame(const QList<std::uint8_t>& pids = {});
    void startCanMonitor();
    void stopCanMonitor();

    /**
     * Envoie une commande ELM/OBD/KWP brute (ex. "010C", "ATDP", "3E").
     * La réponse complète (jusqu'au prompt) arrive via rawResponse.
     * rawLine continue d'émettre chaque ligne.
     */
    void sendRawCommand(const QString& command);

signals:
    void connected(const QString& version);
    void disconnected();
    void errorOccurred(const QString& message);
    void status(const QString& message);
    void pidResult(quint8 pid, double value, const QString& name, const QString& unit);
    void pidUnsupported(quint8 pid);
    /** Bitmap mode 01 : tous les PID déclarés supportés par l'ECU (après probe). */
    void supportedPidsReady(const QList<quint8>& pids);
    void freezeFrameResult(quint8 pid, double value, const QString& name, const QString& unit);
    void dtcsReady(const QStringList& codes, bool pending);
    void vinReady(const QString& vin);
    void canFrame(quint32 id, QByteArray data);
    void rawLine(const QString& line);
    /** Réponse complète d'une sendRawCommand (texte entre envoi et '>'). */
    void rawResponse(const QString& command, const QString& response);

private slots:
    void onReadyRead();
    void onTimeout();
    void onPollTick();
#if defined(ELM_HAVE_BLUETOOTH)
    void onBtConnected();
    void onBtError();
    void onBtLinkLost();
    void onBtConnectTimeout();
#endif

private:
    enum class Kind { Init, Pid, PidSupport, Dtc, ClearDtc, Vin, FreezeFrame, CanStart, Raw };
    struct Cmd { QString text; Kind kind; std::uint8_t pid = 0; };

    void enqueue(const QString& text, Kind kind, std::uint8_t pid = 0);
    void sendNext();
    void writeRaw(const QByteArray& bytes);
    void handleResponse(const Cmd& cmd, const QString& resp);
    void processInitStep(const QString& resp);
    void tryOpenSerial(int baud);
    void failConnect(const QString& why);
    void closeTransport();
    void beginInit();
#if defined(ELM_HAVE_BLUETOOTH)
    void tryNextBtAttempt();
    void startBtSocket();
#endif
    bool isCdcAcmPort() const {
        return m_label.contains(QLatin1String("ACM"), Qt::CaseInsensitive)
            || m_port.contains(QLatin1String("ACM"), Qt::CaseInsensitive);
    }

    QIODevice*      m_io = nullptr;
    QSerialPort*    m_serial = nullptr; // si connectPort
#if defined(ELM_HAVE_BLUETOOTH)
    QBluetoothSocket* m_bt = nullptr;
    QTimer*         m_btConnectTimeout = nullptr;
    QString         m_btAddress;
    int             m_btAttempt = 0; // 0=UUID SPP, 1=ch1, 2=ch2
    bool            m_btTransport = false;
    bool            m_atzRetried = false;
#endif
    bool            m_ownIo = false;
    QTimer*         m_timeout = nullptr;
    QTimer*         m_poll = nullptr;

    QString         m_port;
    QString         m_label;
    int             m_baud = 0;
    bool            m_autoBaud = false;
    bool            m_triedHighBaud = false;
    bool            m_ready = false;
    bool            m_opening = false;
    bool            m_busy = false;
    bool            m_canMode = false;
    int             m_initStep = 0;
    int             m_connectEpoch = 0;
    QString         m_elmVersion;

    QQueue<Cmd>     m_queue;
    Cmd             m_current;
    QByteArray      m_buf;

    QList<std::uint8_t> m_pollPids;
    int                 m_pollIdx = 0;
    QList<std::uint8_t> m_supportedAccum;
    int                 m_supportRemaining = 0;
};

} // namespace elm
