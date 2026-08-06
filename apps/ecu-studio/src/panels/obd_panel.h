#pragma once
#include <QWidget>
#include <QHash>
#include <QString>
#include <cstdint>
#include <optional>
#include <vector>

class QComboBox;
class QPushButton;
class QCheckBox;
class QLabel;
class QTableWidget;
class QPlainTextEdit;
class QFile;
class QTimer;
class QDoubleSpinBox;
class QTabWidget;
class QFrame;
class QVBoxLayout;
class QSpinBox;

#include "ecu/TuneValidation.hpp"

namespace ecu_studio {

class RomDocument;
class Elm327;
class CanTuneValidator;

// Panneau OBD-II / ELM327 : connexion adaptateur, datalog live, validation tune
// (mesuré vs attendu depuis OpenDAMOS), freeze frame, replay CSV, CAN avancé.
class ObdPanel : public QWidget {
    Q_OBJECT
public:
    explicit ObdPanel(RomDocument* doc = nullptr, QWidget* parent = nullptr);
    ~ObdPanel() override;

signals:
    // Bascule vers la map 3D avec le point de fonctionnement live.
    void showMapOn3dRequested(quint32 address, const QString& name,
                              double xPhys, double yPhys,
                              double measured, double expected,
                              const QString& xUnit, const QString& yUnit,
                              const QString& dataUnit);
    void livePointUpdated(quint32 address, int gx, int gy,
                          double measured, double expected);

public slots:
    void refreshValidatorFromDoc();

private slots:
    void refreshPorts();
    void toggleConnect();
    void toggleDatalog();
    void toggleValidation();
    void toggleCanSniff();
    void toggleCsv();
    void readDtcs();
    void readFreezeFrame();
    void clearDtcs();
    void copyDtcs();
    void exportDtcs();
    void onAutoReconnectToggled(bool on);
    void tryAutoReconnect();
    void onToleranceChanged(double v);
    void onYAxisModeChanged(int idx);
    void onShowMap3d();
    void replayValidationCsv();
    void onDriveModeToggled(bool on);
    void onDriveSessionClicked();
    void applyRoutePreset();
    void exportSessionBundle();
    void onCategoryFilterChanged();
    void onRuleCellChanged(int row, int col);
    void launchSocketSpy();

private:
    void buildUi();
    void buildDrivePanel(QVBoxLayout* root);
    void loadSettings();
    void saveSettings();
    void setStatus(const QString& msg, bool error = false);
    void mergeDtcCodes(const QStringList& codes, bool pending);
    void refreshDtcTable();
    void startConnect();
    void scheduleAutoReconnect(const QString& why);
    QString dtcFamily(const QString& code) const;
    QString dtcStatusText(int flags) const;
    QString preferredPort() const;
    void onPidUpdate(quint8 pid, double value);
    void runValidation();
    void startValidation();
    void stopValidation();
    void autoStartCsv();
    void autoStopCsv();
    void tryAutoStartDriveSession();
    void updateValidationTable(const std::vector<ecu::ValidationResult>& results);
    void updateDriveDashboard(const std::vector<ecu::ValidationResult>& results);
    void refreshRulesTable();
    void applyCategoryFilters();
    void showSessionSummary(const ecu::SessionSummary& sum);
    void maybeAlertFail();
    std::optional<ecu::ValidationResult> primaryBoostResult(
        const std::vector<ecu::ValidationResult>& results) const;
    void appendValidationCsv(const std::vector<ecu::ValidationResult>& results);
    ecu::LivePidSnapshot liveSnapshot() const;
    void applyDriveModeUi(bool on);

    RomDocument*    m_doc = nullptr;
    ecu::TuneValidator* m_validator = nullptr;
    ecu::SessionRecorder m_session;
    ecu::StatusHysteresis m_hyst;
    ecu::EmaFilter m_emaMeas;
    ecu::EmaFilter m_emaExp;

    Elm327* m_elm = nullptr;
    QTimer* m_reconnectTimer = nullptr;

    QComboBox*      m_portCombo   = nullptr;
    QComboBox*      m_baudCombo   = nullptr;
    QPushButton*    m_refreshBtn  = nullptr;
    QPushButton*    m_connectBtn  = nullptr;
    QCheckBox*      m_autoReconnect = nullptr;
    QCheckBox*      m_driveModeChk  = nullptr;
    QPushButton*    m_driveBtn      = nullptr;
    QPushButton*    m_presetBtn     = nullptr;
    QPushButton*    m_exportBtn     = nullptr;
    QLabel*         m_statusLabel = nullptr;
    QLabel*         m_romInfoLabel = nullptr;

    QCheckBox*      m_catBoost = nullptr;
    QCheckBox*      m_catSmoke = nullptr;
    QCheckBox*      m_catAir   = nullptr;
    QCheckBox*      m_catFuel  = nullptr;
    QTableWidget*   m_rulesTable = nullptr;
    QSpinBox*       m_alertSpin  = nullptr;
    QCheckBox*      m_beepChk    = nullptr;

    // Mode conduite — tableau de bord grand format (semi-auto)
    QFrame*         m_drivePanel    = nullptr;
    QFrame*         m_driveBanner   = nullptr;
    QLabel*         m_driveVerdict  = nullptr;
    QLabel*         m_boostBig      = nullptr;
    QLabel*         m_boostSub      = nullptr;
    QLabel*         m_rpmLoadLabel  = nullptr;
    QLabel*         m_csvDriveLabel = nullptr;
    QLabel*         m_sessionLiveLabel = nullptr;

    QTabWidget*     m_tabs        = nullptr;
    QTableWidget*   m_pidTable    = nullptr;
    QPushButton*    m_datalogBtn  = nullptr;
    QPushButton*    m_csvBtn      = nullptr;
    QHash<quint8,int> m_pidRow;
    QHash<quint8, double> m_liveValues;

    // Validation tune
    QTableWidget*   m_valTable    = nullptr;
    QPushButton*    m_valBtn      = nullptr;
    QDoubleSpinBox* m_tolSpin     = nullptr;
    QComboBox*      m_yAxisCombo  = nullptr;
    QPushButton*    m_show3dBtn   = nullptr;
    QPushButton*    m_replayBtn   = nullptr;
    QPushButton*    m_freezeBtn   = nullptr;
    QTableWidget*   m_freezeTable = nullptr;
    CanTuneValidator* m_canVal    = nullptr;
    QPushButton*    m_spyBtn      = nullptr;
    bool            m_validating  = false;
    bool            m_driveMode   = true;
    int             m_focusValRow = 0;
    int             m_lastAlertAt = 0;
    QString         m_lastCsvPath;

    QPushButton*    m_dtcReadBtn  = nullptr;
    QPushButton*    m_dtcClearBtn = nullptr;
    QPushButton*    m_dtcCopyBtn  = nullptr;
    QPushButton*    m_dtcExportBtn = nullptr;
    QPushButton*    m_vinBtn      = nullptr;
    QTableWidget*   m_dtcTable    = nullptr;
    QLabel*         m_vinLabel    = nullptr;
    QHash<QString, int> m_dtcFlags;
    int             m_dtcAwaiting = 0;

    QPushButton*    m_canBtn      = nullptr;
    QTableWidget*   m_canTable    = nullptr;
    QHash<quint32,int> m_canRow;

    QPlainTextEdit* m_log         = nullptr;

    QFile*          m_csv         = nullptr;
    bool            m_valCsv      = false;
    QString         m_lastPort;
    bool            m_datalog     = false;
    bool            m_canSniff    = false;
    bool            m_connected   = false;
    bool            m_wantConnected = false;
    bool            m_rulesUiMute = false;
};

} // namespace ecu_studio
