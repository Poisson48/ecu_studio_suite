#pragma once
#include <QWidget>
#include <QHash>
#include <cstdint>

class QComboBox;
class QPushButton;
class QCheckBox;
class QLabel;
class QTableWidget;
class QPlainTextEdit;
class QFile;
class QTimer;

namespace ecu_studio {

class Elm327;

// Panneau OBD-II / ELM327 : connexion à un adaptateur USB, datalog live des PID
// (RPM, boost, températures…), lecture/effacement des codes défaut, VIN, et
// sniffing CAN via ATMA (pas un vrai interface SocketCAN).
class ObdPanel : public QWidget {
    Q_OBJECT
public:
    explicit ObdPanel(QWidget* parent = nullptr);
    ~ObdPanel() override;

private slots:
    void refreshPorts();
    void toggleConnect();
    void toggleDatalog();
    void toggleCanSniff();
    void toggleCsv();
    void readDtcs();
    void clearDtcs();
    void copyDtcs();
    void exportDtcs();
    void onAutoReconnectToggled(bool on);
    void tryAutoReconnect();

private:
    void buildUi();
    void setStatus(const QString& msg, bool error = false);
    void mergeDtcCodes(const QStringList& codes, bool pending);
    void refreshDtcTable();
    void startConnect();
    void scheduleAutoReconnect(const QString& why);
    QString dtcFamily(const QString& code) const;
    QString dtcStatusText(int flags) const;
    QString preferredPort() const;

    Elm327* m_elm = nullptr;
    QTimer* m_reconnectTimer = nullptr;

    QComboBox*      m_portCombo   = nullptr;
    QComboBox*      m_baudCombo   = nullptr;
    QPushButton*    m_refreshBtn  = nullptr;
    QPushButton*    m_connectBtn  = nullptr;
    QCheckBox*      m_autoReconnect = nullptr;
    QLabel*         m_statusLabel = nullptr;

    QTableWidget*   m_pidTable    = nullptr;   // dashboard live
    QPushButton*    m_datalogBtn  = nullptr;
    QPushButton*    m_csvBtn      = nullptr;
    QHash<quint8,int> m_pidRow;                // pid -> ligne du tableau

    QPushButton*    m_dtcReadBtn  = nullptr;
    QPushButton*    m_dtcClearBtn = nullptr;
    QPushButton*    m_dtcCopyBtn  = nullptr;
    QPushButton*    m_dtcExportBtn = nullptr;
    QPushButton*    m_vinBtn      = nullptr;
    QTableWidget*   m_dtcTable    = nullptr;   // code / famille / statut
    QLabel*         m_vinLabel    = nullptr;
    // bit0 = mémorisé (03), bit1 = en attente (07)
    QHash<QString, int> m_dtcFlags;
    int             m_dtcAwaiting = 0;         // réponses 03/07 encore attendues

    QPushButton*    m_canBtn      = nullptr;
    QTableWidget*   m_canTable    = nullptr;   // trames CAN (sniff)
    QHash<quint32,int> m_canRow;

    QPlainTextEdit* m_log         = nullptr;

    QFile*          m_csv         = nullptr;
    QString         m_lastPort;                // port cible auto-reconnect
    bool            m_datalog     = false;
    bool            m_canSniff    = false;
    bool            m_connected   = false;
    bool            m_wantConnected = false;   // true tant qu'on veut rester en ligne
};

} // namespace ecu_studio
