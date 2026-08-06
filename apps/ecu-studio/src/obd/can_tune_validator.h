#pragma once
// ─── CanTuneValidator ───────────────────────────────────────────────────────
// Validation continue d'un signal CAN via socketspy-mcp (phase 5).
// Widget embarqué dans ObdPanel : poll can_monitor et compare à une cible.

#include <QWidget>
#include <QJsonObject>
#include <QString>
#include <QHash>

class QComboBox;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QPlainTextEdit;
class QLabel;
class QProcess;
class QTcpSocket;
class QTimer;

namespace ecu_studio {

class CanTuneValidator : public QWidget {
    Q_OBJECT
public:
    explicit CanTuneValidator(QWidget* parent = nullptr);
    ~CanTuneValidator() override;

    void setTargetSignal(const QString& signal, const QString& unit = {});

signals:
    void measuredValueChanged(double value, double target, double delta);

private slots:
    void onStartStop();
    void onProcStarted();
    void onProcErrorOccurred(int err);
    void onSocketConnected();
    void onSocketReadyRead();
    void onSocketError();
    void onPollTick();

private:
    void buildUi();
    void log(const QString& msg, bool error = false);
    static QStringList detectInterfaces();
    void refreshInterfaces();
    void startMonitoring();
    void stopMonitoring(bool ok, const QString& reason);
    int  sendRequest(const QString& method, const QJsonObject& params);
    int  callTool(const QString& toolName, const QJsonObject& arguments);
    void handleResponse(const QJsonObject& response);
    static QJsonObject extractToolPayload(const QJsonObject& result);
    void requestSample();

    QProcess*    m_proc{nullptr};
    QTcpSocket*  m_socket{nullptr};
    QTimer*      m_pollTimer{nullptr};
    quint16      m_port{0};
    QByteArray   m_rxBuffer;
    int          m_nextId{1};
    QHash<int, QString> m_pending;
    bool         m_initialized{false};
    bool         m_running{false};

    QComboBox*      m_ifaceCombo{nullptr};
    QLineEdit*      m_signalEdit{nullptr};
    QDoubleSpinBox* m_targetSpin{nullptr};
    QDoubleSpinBox* m_toleranceSpin{nullptr};
    QSpinBox*       m_intervalSpin{nullptr};
    QPushButton*    m_startBtn{nullptr};
    QPlainTextEdit* m_logView{nullptr};
    QLabel*         m_statusLabel{nullptr};
    QLabel*         m_liveLabel{nullptr};
};

} // namespace ecu_studio
