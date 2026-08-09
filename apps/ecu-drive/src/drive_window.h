#pragma once
#include <QMainWindow>
#include <QHash>
#include <optional>
#include <vector>

#include "ecu/TuneValidation.hpp"

#if defined(ELM_HAVE_BLUETOOTH)
#  include <QBluetoothDeviceDiscoveryAgent>
#  include <QBluetoothDeviceInfo>
#endif

class QLabel;
class QPushButton;
class QComboBox;
class QListWidget;
class QCheckBox;
class QFrame;
class QFile;
class QProgressBar;
class QWidget;
class QStackedWidget;
class QScrollArea;


namespace elm { class Elm327; }

namespace ecu_drive {

class Updater;

class DriveWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit DriveWindow(QWidget* parent = nullptr);
    ~DriveWindow() override;

    /** Charge un .ecutune ou une ROM .bin/.hex (ex. --tune CLI). */
    void loadTuneFile(const QString& path);
    /** Bypass du sélecteur ECU (tests / --ecu). */
    void setAutoEcuId(const QString& ecuId) { m_autoEcuId = ecuId; }

private slots:
    void importTune();
    void refreshPorts();
    void startBtScan();
    void toggleConnect();
    void toggleSession();
    void onPid(quint8 pid, double value, const QString& name, const QString& unit);
    void onUpdaterState();
    void onUpdateAction();
    void onUpdateDismiss();
    void checkUpdatesManual();
    void showDrivePage();
    void showSensorsPage();
#if defined(ELM_HAVE_BLUETOOTH)
    void rebuildBtCombo();
#endif

private:
    void buildUi();
    QWidget* buildDrivePage(QWidget* parent);
    QWidget* buildSensorsPage(QWidget* parent);
    void setStatus(const QString& msg, bool error = false);
    /** Affiche une barre de progression (indéterminée si max<=0). */
    void beginBusy(const QString& message, int max = 0);
    void setBusy(int value, const QString& message = {});
    void endBusy();
    void layoutBusyOverlay();
    bool eventFilter(QObject* watched, QEvent* event) override;
    void applyTunePackage(const ecu::TunePackage& pkg, const QString& path);
    void applyRomBinary(const QByteArray& rom, const QString& ecuId, const QString& path);
    /** Liste les ECU ayant une recette OpenDAMOS (qrc + disque), mise en cache. */
    QStringList availableEcuIds();
    /**
     * Sélecteur ECU in-app (pas QInputDialog) — sous Android les dialogues
     * modaux Qt ne peignent / n'acceptent OK qu'après un changement d'app.
     */
    void showEcuPicker(const QByteArray& rom, const QString& path,
                       const QString& hint = {});
    void hideEcuPicker();
    void onEcuPickerOk();
    void onEcuPickerCancel();
    /** Dialogue info in-app (Android-safe, remplace QMessageBox). */
    void showInfoDialog(const QString& title, const QString& body,
                        const QString& okLabel = {});
    void hideInfoDialog();
    bool loadRomBinaryFile(const QString& path);
    void startSession();
    void stopSession();
    void runValidation();
    void updateDriveUi(const std::vector<ecu::ValidationResult>& results);
    void refreshSensorsTable();
    void ensureSensorsPolling();
    void showSummary(const ecu::SessionSummary& sum);
    /** Dossier scratch (toujours writable) pour l'enregistrement live. */
    QString sessionLogScratchDir() const;
    /** Dernier dossier « Enregistrer sous » (suggestion), sinon Téléchargements. */
    QString suggestedLogSaveDir() const;
    /**
     * Dialogue Enregistrer sous → copie le CSV scratch vers un chemin
     * accessible (SAF / content:// sous Android). Retourne le chemin choisi
     * ou QString() si annulé / échec.
     */
    QString promptSaveLogAs(const QString& sourceCsv);
    void autoStartCsv();
    void autoStopCsv();
    void appendCsv(const std::vector<ecu::ValidationResult>& results);
    void shareLastLog();
    void maybeAlert();
    void refreshUpdateBanner();
#if defined(ELM_HAVE_BLUETOOTH)
    void ensureBtAgent();
    void setBtScanning(bool on);
    void refreshBtScanStatus();
    void showBtDevicePicker();
    void hideBtDevicePicker();
    void onBtPickerOk();
    void onBtPickerCancel();
    void selectBestBtDevice();
    static bool likelyElmBtName(const QString& name);
    void selectLastBtDevice();
    struct BtDevice {
        QString addr;
        QString name;
        bool likelyObd = false;
    };
#endif
    std::optional<ecu::ValidationResult> primaryBoost(
        const std::vector<ecu::ValidationResult>& results) const;
    ecu::LivePidSnapshot snapshot() const;

    elm::Elm327* m_elm = nullptr;
    Updater* m_updater = nullptr;
    ecu::TuneValidator m_validator;
    ecu::SessionRecorder m_session;
    ecu::StatusHysteresis m_hyst;
    ecu::EmaFilter m_emaMeas;
    ecu::EmaFilter m_emaExp;

    QStackedWidget* m_stack = nullptr;
    QLabel*  m_tuneLabel = nullptr;
    QLabel*  m_statusLabel = nullptr;
    QWidget* m_busyOverlay = nullptr;
    QFrame*  m_busyFrame = nullptr;
    QLabel*  m_busyLabel = nullptr;
    QLabel*  m_busyDetail = nullptr;
    QProgressBar* m_busyBar = nullptr;
    QTimer*  m_busyPulse = nullptr;
    qint64   m_busyStartedMs = 0;
    QWidget* m_ecuPickerOverlay = nullptr;
    QComboBox* m_ecuPickerCombo = nullptr;
    QWidget* m_btPickerOverlay = nullptr;
    QListWidget* m_btPickerList = nullptr;
    QWidget* m_infoOverlay = nullptr;
    QLabel*  m_infoTitle = nullptr;
    QLabel*  m_infoBody = nullptr;
    QPushButton* m_infoOkBtn = nullptr;
    QByteArray m_pendingRom;
    QString m_pendingRomPath;
    QString m_autoEcuId;
    /** Une fois : rechargement lastTune au démarrage sans redemander l'ECU. */
    bool m_suppressEcuPromptOnce = false;
    QComboBox* m_portCombo = nullptr;
    QComboBox* m_btCombo = nullptr;
    QCheckBox* m_btObdOnlyChk = nullptr;
    QPushButton* m_connectBtn = nullptr;
    QPushButton* m_scanBtBtn = nullptr;
    QPushButton* m_sessionBtn = nullptr;
    QFrame*  m_banner = nullptr;
    QLabel*  m_verdict = nullptr;
    QLabel*  m_boostBig = nullptr;
    QLabel*  m_boostSub = nullptr;
    QLabel*  m_rpmLoad = nullptr;
    QLabel*  m_sessionLive = nullptr;
    QLabel*  m_csvLabel = nullptr;
    QCheckBox* m_beepChk = nullptr;
    QLabel* m_sensorsStatus = nullptr;
    QHash<quint8, QLabel*> m_sensorValueLabels;

    QFrame*       m_updateBanner = nullptr;
    QLabel*       m_updateTitle = nullptr;
    QLabel*       m_updateSub = nullptr;
    QProgressBar* m_updateProgress = nullptr;
    QPushButton*  m_updateActionBtn = nullptr;
    QPushButton*  m_updateDismissBtn = nullptr;

    QHash<quint8, double> m_live;
    QHash<quint8, QString> m_liveUnit;
    bool m_connected = false;
    bool m_sessionOn = false;
    bool m_userDisconnect = false;
    bool m_linkLossNotified = false;
    QString m_pendingDisconnectReason;
    int  m_busyDepth = 0;
    QStringList m_ecuIdsCache;
    QString m_tunePath;
    QString m_lastCsv;
    QFile* m_csv = nullptr;
    int m_lastAlertAt = 0;

#if defined(ELM_HAVE_BLUETOOTH)
    QBluetoothDeviceDiscoveryAgent* m_btAgent = nullptr;
    std::vector<BtDevice> m_btDevices;
    QTimer* m_btScanPulse = nullptr;
    QTimer* m_btScanWatchdog = nullptr;
    qint64  m_btScanStartedMs = 0;
    bool    m_btScanning = false;
#endif
};

} // namespace ecu_drive
