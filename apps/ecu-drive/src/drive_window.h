#pragma once
#include <QMainWindow>
#include <QHash>
#include <optional>
#include <vector>

#include "ecu/TuneValidation.hpp"

#if defined(ELM_HAVE_BLUETOOTH)
#  include <QBluetoothDeviceDiscoveryAgent>
#endif

class QLabel;
class QPushButton;
class QComboBox;
class QCheckBox;
class QFrame;
class QFile;
class QProgressBar;

namespace elm { class Elm327; }

namespace ecu_drive {

class Updater;

class DriveWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit DriveWindow(QWidget* parent = nullptr);
    ~DriveWindow() override;

    /** Charge un .ecutune (ex. --tune CLI). */
    void loadTuneFile(const QString& path);

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

private:
    void buildUi();
    void setStatus(const QString& msg, bool error = false);
    void applyTunePackage(const ecu::TunePackage& pkg, const QString& path);
    void startSession();
    void stopSession();
    void runValidation();
    void updateDriveUi(const std::vector<ecu::ValidationResult>& results);
    void showSummary(const ecu::SessionSummary& sum);
    void autoStartCsv();
    void autoStopCsv();
    void appendCsv(const std::vector<ecu::ValidationResult>& results);
    void shareLastLog();
    void maybeAlert();
    void refreshUpdateBanner();
#if defined(ELM_HAVE_BLUETOOTH)
    void ensureBtAgent();
    static bool likelyElmBtName(const QString& name);
    void selectLastBtDevice();
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

    QLabel*  m_tuneLabel = nullptr;
    QLabel*  m_statusLabel = nullptr;
    QComboBox* m_portCombo = nullptr;
    QComboBox* m_btCombo = nullptr;
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

    QFrame*       m_updateBanner = nullptr;
    QLabel*       m_updateTitle = nullptr;
    QLabel*       m_updateSub = nullptr;
    QProgressBar* m_updateProgress = nullptr;
    QPushButton*  m_updateActionBtn = nullptr;
    QPushButton*  m_updateDismissBtn = nullptr;

    QHash<quint8, double> m_live;
    bool m_connected = false;
    bool m_sessionOn = false;
    QString m_tunePath;
    QString m_lastCsv;
    QFile* m_csv = nullptr;
    int m_lastAlertAt = 0;

#if defined(ELM_HAVE_BLUETOOTH)
    QBluetoothDeviceDiscoveryAgent* m_btAgent = nullptr;
#endif
};

} // namespace ecu_drive
