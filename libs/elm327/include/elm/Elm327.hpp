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
    // Desktop : UUID SerialPort → canal RFCOMM 1 → canal 2.
    // Android : BluetoothSocket natif (Qt refuse le connect par canal).
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

    /**
     * Security Access automatisé (UDS 0x27 ou KWP 0x27).
     * Envoie la requête seed, calcule la key via l'algorithme choisi,
     * puis envoie la key — tout en séquence.
     *
     * @param algo       Algorithme : "PSA", "VAG_SA2", "Daimler", "BoschGeneric", "GenericXOR"
     * @param level      Niveau d'accès : 1 = download/flash, 2 = config/zones
     * @param ecuKey     Clé ECU 2 octets (requis pour PSA, ignoré sinon)
     * @param isKwp      true = KWP2000 (27 81/82), false = UDS (27 01/02)
     *
     * Émet securityAccessResult(success, keyHex) à la fin.
     */
    void sendSecurityAccessRequest(const QString& algo,
                                   int level = 1,
                                   quint16 ecuKey = 0,
                                   bool isKwp = false);

    /**
     * Lit une zone ECU via KWP2000 service 21 (PSA / VAG).
     * Émet rawResponse avec la réponse brute.
     * @param zoneId  Numéro de zone (1 octet, ex. 0x21 = identification)
     */
    void readEcuZone(quint8 zoneId);

    /**
     * Écrit une zone ECU via KWP2000 service 3B (PSA).
     * L'ECU doit être déverrouillé (sendSecurityAccessRequest niveau 2) avant.
     * @param zoneId  Numéro de zone
     * @param data    Données à écrire
     */
    void writeEcuZone(quint8 zoneId, const QByteArray& data);

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
    /** Résultat d'un sendSecurityAccessRequest. keyHex = key calculée et envoyée. */
    void securityAccessResult(bool success, const QString& keyHex, const QString& detail);

private slots:
    void onReadyRead();
    void onTimeout();
    void onPollTick();
#if defined(ELM_HAVE_BLUETOOTH)
    void onBtConnected();
    void onBtError();
    void onBtLinkLost();
    void onBtConnectTimeout();
#ifdef Q_OS_ANDROID
    void onAndroidBtResult(const QString& result, int gen);
#endif
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
#ifdef Q_OS_ANDROID
    void startAndroidNativeBt(int mode);
#endif
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
    int             m_btGen = 0;
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
