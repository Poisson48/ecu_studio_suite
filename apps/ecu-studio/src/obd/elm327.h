#pragma once
//
// Elm327 — driver série robuste et tolérant-clone pour adaptateurs ELM327 USB.
//
// Conçu pour les clones chinois (CH340 / FTDI / CP210x / PL2303) :
//   - énumération + détection du port par VID/PID du pont USB-série ;
//   - auto-baud (38400 puis 115200, les deux valeurs usine des clones) ;
//   - init PILOTÉE PAR LE PROMPT « > » (pas de délais fixes fragiles) avec timeout
//     et reprise ;
//   - file de commandes ; deux modes : requêtes OBD-II PID (datalog) et monitor CAN
//     brut (ATMA, sniffing).
//
// L'interprétation OBD-II (PID, DTC, VIN) est déléguée à ecu::obd2 (testé).
//
#include <QObject>
#include <QByteArray>
#include <QList>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <cstdint>

class QSerialPort;
class QTimer;

namespace ecu_studio {

struct SerialPortDesc {
    QString port;          // ex. /dev/ttyUSB0 ou COM3
    QString description;   // libellé matériel
    bool    likelyElm = false;  // pont USB-série typique d'un ELM327
};

class Elm327 : public QObject {
    Q_OBJECT
public:
    explicit Elm327(QObject* parent = nullptr);
    ~Elm327() override;

    // Liste les ports série, en marquant ceux dont le pont USB ressemble à un ELM327.
    static QList<SerialPortDesc> listPorts();

    // Connecte + initialise l'ELM327. baud == 0 → auto (38400 puis 115200).
    void connectPort(const QString& port, int baud = 0);
    void disconnectPort();
    bool isReady() const { return m_ready; }

    // ── OBD-II ───────────────────────────────────────────────────────────────
    void queryPid(std::uint8_t pid);                 // une lecture
    void startPolling(const QList<std::uint8_t>& pids, int intervalMs = 250);
    void stopPolling();
    void readDtcs();
    void clearDtcs();
    void readVin();

    // ── Sniffing CAN (ATMA) ──────────────────────────────────────────────────
    void startCanMonitor();
    void stopCanMonitor();

signals:
    void connected(const QString& version);
    void disconnected();
    void errorOccurred(const QString& message);
    void status(const QString& message);
    void pidResult(quint8 pid, double value, const QString& name, const QString& unit);
    void pidUnsupported(quint8 pid);
    void dtcsReady(const QStringList& codes);
    void vinReady(const QString& vin);
    void canFrame(quint32 id, QByteArray data);
    void rawLine(const QString& line);   // toute ligne reçue (debug / journal)

private slots:
    void onReadyRead();
    void onTimeout();
    void onPollTick();

private:
    enum class Kind { Init, Pid, Dtc, ClearDtc, Vin, CanStart, Raw };
    struct Cmd { QString text; Kind kind; std::uint8_t pid = 0; };

    void enqueue(const QString& text, Kind kind, std::uint8_t pid = 0);
    void sendNext();
    void writeRaw(const QByteArray& bytes);
    void handleResponse(const Cmd& cmd, const QString& resp);
    void processInitStep(const QString& resp);
    void tryOpen(int baud);
    void failConnect(const QString& why);

    QSerialPort*    m_serial = nullptr;
    QTimer*         m_timeout = nullptr;
    QTimer*         m_poll = nullptr;
    QByteArray      m_buf;
    QQueue<Cmd>     m_queue;
    Cmd             m_current{};
    bool            m_busy = false;     // une commande est en cours (attend « > »)
    bool            m_ready = false;    // init terminée
    bool            m_canMode = false;  // monitor CAN actif (flux continu, pas de « > »)

    QString         m_port;
    int             m_baud = 0;
    int             m_initStep = 0;
    bool            m_autoBaud = false;
    bool            m_triedHighBaud = false;

    QList<std::uint8_t> m_pollPids;
    int                 m_pollIdx = 0;
};

} // namespace ecu_studio
