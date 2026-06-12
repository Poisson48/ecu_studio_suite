#pragma once
#include <QWidget>
#include <QHash>
#include <cstdint>

class QComboBox;
class QPushButton;
class QLabel;
class QTableWidget;
class QPlainTextEdit;
class QFile;

namespace ecu_studio {

class Elm327;

// Panneau OBD-II / ELM327 : connexion à un adaptateur USB, datalog live des PID
// (RPM, boost, températures…), lecture/effacement des codes défaut, VIN, et
// sniffing CAN brut. Pensé pour vérifier l'effet d'un tune en temps réel.
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

private:
    void buildUi();
    void setStatus(const QString& msg, bool error = false);

    Elm327* m_elm = nullptr;

    QComboBox*      m_portCombo   = nullptr;
    QComboBox*      m_baudCombo   = nullptr;
    QPushButton*    m_refreshBtn  = nullptr;
    QPushButton*    m_connectBtn  = nullptr;
    QLabel*         m_statusLabel = nullptr;

    QTableWidget*   m_pidTable    = nullptr;   // dashboard live
    QPushButton*    m_datalogBtn  = nullptr;
    QPushButton*    m_csvBtn      = nullptr;
    QHash<quint8,int> m_pidRow;                // pid -> ligne du tableau

    QPushButton*    m_dtcReadBtn  = nullptr;
    QPushButton*    m_dtcClearBtn = nullptr;
    QPushButton*    m_vinBtn      = nullptr;
    QLabel*         m_dtcLabel    = nullptr;
    QLabel*         m_vinLabel    = nullptr;

    QPushButton*    m_canBtn      = nullptr;
    QTableWidget*   m_canTable    = nullptr;   // trames CAN (sniff)
    QHash<quint32,int> m_canRow;

    QPlainTextEdit* m_log         = nullptr;

    QFile*          m_csv         = nullptr;
    bool            m_datalog     = false;
    bool            m_canSniff    = false;
    bool            m_connected   = false;
};

} // namespace ecu_studio
