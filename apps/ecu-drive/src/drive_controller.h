#pragma once
#include <QObject>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QStringList>
#include <functional>
#include <optional>
#include <vector>

#include "ecu/TuneValidation.hpp"

#if defined(ELM_HAVE_BLUETOOTH)
#  include <QBluetoothDeviceDiscoveryAgent>
#  include <QBluetoothDeviceInfo>
#endif

namespace elm  { class Elm327; }
namespace ecu_drive { class Updater; }

namespace ecu_drive {

/**
 * Contrôleur principal ECU Drive — QObject pur, sans UI.
 * Exposé au contexte QML via setContextProperty("Drive", &controller).
 *
 * Propriétés QML : connected, sessionOn, tuneLabel, statusText,
 *   btDevices, btScanning, ports, sensorValues, dtcList,
 *   turboMap/Baro/Delta/Maf/Rpm, rawLog, saResult.
 */
class DriveController : public QObject {
    Q_OBJECT

    // ── Connexion ──────────────────────────────────────────────────────────
    Q_PROPERTY(bool    connected    READ connected    NOTIFY connectedChanged)
    Q_PROPERTY(bool    sessionOn    READ sessionOn    NOTIFY sessionChanged)
    Q_PROPERTY(bool    btScanning   READ btScanning   NOTIFY btScanningChanged)
    Q_PROPERTY(QStringList ports    READ ports        NOTIFY portsChanged)
    Q_PROPERTY(QString selectedPort READ selectedPort WRITE setSelectedPort NOTIFY selectedPortChanged)
    Q_PROPERTY(QVariantList btDevices READ btDevices  NOTIFY btDevicesChanged)
    Q_PROPERTY(QString selectedBt   READ selectedBt   WRITE setSelectedBt   NOTIFY selectedBtChanged)
    Q_PROPERTY(bool    btObdOnly    READ btObdOnly    WRITE setBtObdOnly    NOTIFY btObdOnlyChanged)

    // ── Tune / ROM ─────────────────────────────────────────────────────────
    Q_PROPERTY(QString tuneLabel    READ tuneLabel    NOTIFY tuneLabelChanged)
    Q_PROPERTY(QString tunePath     READ tunePath     NOTIFY tuneLabelChanged)
    Q_PROPERTY(bool    tuneReady    READ tuneReady    NOTIFY tuneLabelChanged)

    // ── Status / UI ────────────────────────────────────────────────────────
    Q_PROPERTY(QString statusText   READ statusText   NOTIFY statusChanged)
    Q_PROPERTY(bool    statusError  READ statusError  NOTIFY statusChanged)
    Q_PROPERTY(bool    busy         READ busy         NOTIFY busyChanged)
    Q_PROPERTY(QString busyLabel    READ busyLabel    NOTIFY busyChanged)
    Q_PROPERTY(int     busyMax      READ busyMax      NOTIFY busyChanged)
    Q_PROPERTY(int     busyValue    READ busyValue    NOTIFY busyChanged)

    // ── Conduite (live boost) ──────────────────────────────────────────────
    Q_PROPERTY(QString verdict      READ verdict      NOTIFY driveChanged)
    Q_PROPERTY(QString boostBig     READ boostBig     NOTIFY driveChanged)
    Q_PROPERTY(QString boostSub     READ boostSub     NOTIFY driveChanged)
    Q_PROPERTY(QString rpmLoad      READ rpmLoad      NOTIFY driveChanged)
    Q_PROPERTY(QString sessionLive  READ sessionLive  NOTIFY driveChanged)
    Q_PROPERTY(QVariantList mapsList READ mapsList    NOTIFY driveChanged)

    // ── Capteurs OBD ──────────────────────────────────────────────────────
    Q_PROPERTY(QVariantList sensorValues READ sensorValues NOTIFY sensorValuesChanged)

    // ── Turbo / Diagnostic ─────────────────────────────────────────────────
    Q_PROPERTY(QString turboMap     READ turboMap     NOTIFY turboChanged)
    Q_PROPERTY(QString turboBaro    READ turboBaro    NOTIFY turboChanged)
    Q_PROPERTY(QString turboDelta   READ turboDelta   NOTIFY turboChanged)
    Q_PROPERTY(QString turboMaf     READ turboMaf     NOTIFY turboChanged)
    Q_PROPERTY(QString turboRpm     READ turboRpm     NOTIFY turboChanged)

    // ── Console OBD / DTC ─────────────────────────────────────────────────
    Q_PROPERTY(QString rawLog       READ rawLog       NOTIFY rawLogChanged)
    Q_PROPERTY(QVariantList dtcList READ dtcList      NOTIFY dtcListChanged)
    Q_PROPERTY(bool dtcReadEnabled  READ dtcReadEnabled  NOTIFY connectedChanged)
    Q_PROPERTY(bool dtcClearEnabled READ dtcClearEnabled NOTIFY dtcListChanged)

    // ── Security Access ────────────────────────────────────────────────────
    Q_PROPERTY(QString saResult     READ saResult     NOTIFY saResultChanged)

    // ── Alertes ────────────────────────────────────────────────────────────
    Q_PROPERTY(bool beepAlert       READ beepAlert    WRITE setBeepAlert    NOTIFY beepAlertChanged)

public:
    explicit DriveController(elm::Elm327* elm, Updater* updater, QObject* parent = nullptr);
    ~DriveController() override;

    // Accesseurs propriétés
    bool       connected()    const { return m_connected; }
    bool       sessionOn()    const { return m_sessionOn; }
    bool       btScanning()   const { return m_btScanning; }
    QStringList ports()       const { return m_ports; }
    QString    selectedPort() const { return m_selectedPort; }
    QVariantList btDevices()  const;
    QString    selectedBt()   const { return m_selectedBt; }
    bool       btObdOnly()    const { return m_btObdOnly; }

    QString    tuneLabel()    const { return m_tuneLabel; }
    QString    tunePath()     const { return m_tunePath; }
    bool       tuneReady()    const { return m_tuneReady; }

    QString    statusText()   const { return m_statusText; }
    bool       statusError()  const { return m_statusError; }
    bool       busy()         const { return m_busyDepth > 0; }
    QString    busyLabel()    const { return m_busyLabel; }
    int        busyMax()      const { return m_busyMax; }
    int        busyValue()    const { return m_busyValue; }

    QString    verdict()      const { return m_verdict; }
    QString    boostBig()     const { return m_boostBig; }
    QString    boostSub()     const { return m_boostSub; }
    QString    rpmLoad()      const { return m_rpmLoad; }
    QString    sessionLive()  const { return m_sessionLive; }
    QVariantList mapsList()   const { return m_mapsList; }

    QVariantList sensorValues() const { return m_sensorValues; }

    QString    turboMap()     const { return m_turboMap; }
    QString    turboBaro()    const { return m_turboBaro; }
    QString    turboDelta()   const { return m_turboDelta; }
    QString    turboMaf()     const { return m_turboMaf; }
    QString    turboRpm()     const { return m_turboRpm; }

    QString    rawLog()       const { return m_rawLog; }
    QVariantList dtcList()    const { return m_dtcList; }
    bool dtcReadEnabled()     const { return m_connected; }
    bool dtcClearEnabled()    const { return !m_dtcList.isEmpty(); }

    QString    saResult()     const { return m_saResult; }
    bool       beepAlert()    const { return m_beepAlert; }

    void setSelectedBt(const QString& addr);
    void setSelectedPort(const QString& port);
    void setBtObdOnly(bool on);
    void setBeepAlert(bool on);

    /** Charge un .ecutune ou une ROM .bin (appelé depuis main.cpp --tune). */
    Q_INVOKABLE void loadTuneFile(const QString& path);
    Q_INVOKABLE QStringList availableEcuIds();
    Q_INVOKABLE void setAutoEcuId(const QString& id) { m_autoEcuId = id; }

signals:
    void connectedChanged();
    void sessionChanged();
    void btScanningChanged();
    void portsChanged();
    void btDevicesChanged();
    void selectedPortChanged();
    void selectedBtChanged();
    void btObdOnlyChanged();
    void tuneLabelChanged();
    void statusChanged();
    void busyChanged();
    void driveChanged();
    void sensorValuesChanged();
    void turboChanged();
    void rawLogChanged();
    void dtcListChanged();
    void saResultChanged();
    void beepAlertChanged();
    /** Demande à QML d'ouvrir un FileDialog. QML rappelle loadTuneFile(path). */
    void requestFilePicker();
    /** Demande d'affichage d'un dialog info (titre, corps, okLabel). */
    void showDialog(const QString& title, const QString& body, const QString& okLabel);
    /** Demande de sélection ECU (liste d'IDs). */
    void showEcuPicker(const QStringList& ecuIds, const QString& hint);
    /** Demande de sélection device BT. */
    void showBtPicker();
    /** Toast Android natif. */
    void toast(const QString& message);

public slots:
    // Connexion
    void toggleConnect();
    void refreshPorts();
    void startBtScan();
    void ecuPickerAccepted(const QString& ecuId);
    void ecuPickerCancelled();

    // Session
    void toggleSession();

    // Diagnostic
    void readDtcs();
    void clearDtcs();
    Q_INVOKABLE QString copyDtcs() const;
    void sendRawCommand(const QString& cmd);
    void computeSaKey(const QString& proto, const QString& seedHex, const QString& ecuKeyHex);
    void sendSecurityAccess(const QString& proto, int level, const QString& ecuKeyHex, bool kwp);
    void sendActuatorOn(const QString& cmd);
    void sendActuatorOff(const QString& cmd);

    // Tune
    void importTune();

    // MAJ
    void checkUpdates();
    // Capteurs
    Q_INVOKABLE void ensureSensorsPolling();

private:
    void onPid(quint8 pid, double value, const QString& name, const QString& unit);
    void onRawResponse(const QString& cmd, const QString& resp);
    void appendRawLog(const QString& line);
    void setStatus(const QString& msg, bool error = false);
    void beginBusy(const QString& msg, int max = 0);
    void setBusy(int value, const QString& msg = {});
    void endBusy();
    void mergeDtcCodes(const QStringList& codes, bool pending);
    void refreshDtcList();
    void ensureTurboPolling();
    void refreshTurboLive();
    void refreshSensorsTable();
    void startSession();
    void stopSession();
    void runValidation();
    void updateDriveUi(const std::vector<ecu::ValidationResult>& results);
    void refreshMapsList(const std::vector<ecu::ValidationResult>& results);
    void maybeAlert();
    void autoStartCsv();
    void autoStopCsv();
    void appendCsv(const std::vector<ecu::ValidationResult>& results);
    void applyTunePackage(const ecu::TunePackage& pkg, const QString& path);
    bool applyRomBinary(const QByteArray& rom, const QString& ecuId, const QString& path);
    ecu::LivePidSnapshot snapshot() const;
    std::optional<ecu::ValidationResult> primaryBoost(
        const std::vector<ecu::ValidationResult>& results) const;
    static QString csvSanitizeMapName(const QString& name);
    QString sessionLogScratchDir() const;
    QString promptSaveLogAs(const QString& sourceCsv);
    void shareLastLog();

#if defined(ELM_HAVE_BLUETOOTH)
    void ensureBtAgent();
    void setBtScanningState(bool on);
    void selectLastBtDevice();
    void toggleConnectAfterPerms();
    static bool likelyElmBtName(const QString& name);
    struct BtDevice { QString addr; QString name; bool likelyObd = false; };
#endif

    elm::Elm327*  m_elm     = nullptr;
    Updater*      m_updater = nullptr;

    ecu::TuneValidator   m_validator;
    ecu::SessionRecorder m_session;
    ecu::StatusHysteresis m_hyst;
    ecu::EmaFilter        m_emaMeas;
    ecu::EmaFilter        m_emaExp;

    // État connexion
    bool    m_connected = false;
    bool    m_sessionOn = false;
    bool    m_uiSuspended = false;
    bool    m_userDisconnect = false;
    bool    m_linkLossNotified = false;
    QString m_pendingDisconnectReason;

    // BT
    QString     m_selectedBt;
    QString     m_selectedPort;
    bool        m_btObdOnly = true;
    bool        m_btScanning = false;
    QStringList m_ports;
#if defined(ELM_HAVE_BLUETOOTH)
    QBluetoothDeviceDiscoveryAgent* m_btAgent = nullptr;
    std::vector<BtDevice> m_btDevices;
    QTimer* m_btScanPulse = nullptr;
    QTimer* m_btScanWatchdog = nullptr;
    qint64  m_btScanStartedMs = 0;
#endif

    // Tune
    QString    m_tuneLabel = QObject::tr("Aucun tune — importe un .ecutune ou une ROM .bin");
    QString    m_tunePath;
    bool       m_tuneReady = false;
    QByteArray m_pendingRom;
    QString    m_pendingRomPath;
    QString    m_autoEcuId;
    bool       m_suppressEcuPromptOnce = false;
    QStringList m_ecuIdsCache;

    // Status / busy
    QString m_statusText;
    bool    m_statusError = false;
    int     m_busyDepth = 0;
    QString m_busyLabel;
    int     m_busyMax = 0;
    int     m_busyValue = 0;

    // Conduite live
    QString      m_verdict;
    QString      m_boostBig  = QStringLiteral("—");
    QString      m_boostSub;
    QString      m_rpmLoad;
    QString      m_sessionLive;
    QVariantList m_mapsList;

    // Capteurs
    QVariantList m_sensorValues;
    QHash<quint8, double>  m_live;
    QHash<quint8, QString> m_liveUnit;
    QSet<quint8>           m_ecuSupportedPids;

    // Turbo
    QString m_turboMap   = QStringLiteral("—");
    QString m_turboBaro  = QStringLiteral("—");
    QString m_turboDelta = QStringLiteral("—");
    QString m_turboMaf   = QStringLiteral("—");
    QString m_turboRpm   = QStringLiteral("—");

    // Console / DTC
    QString      m_rawLog;
    QVariantList m_dtcList;
    QHash<QString, int> m_dtcFlags;
    int  m_dtcAwaiting   = 0;
    bool m_dtcClearPending = false;

    // Security Access
    QString m_saResult;

    // CSV / alertes
    QString     m_lastCsv;
    QFile*      m_csv = nullptr;
    QStringList m_csvMaps;
    qint64      m_lastCsvWriteMs = 0;
    int         m_lastAlertAt = 0;
    bool        m_beepAlert = true;
};

} // namespace ecu_drive
