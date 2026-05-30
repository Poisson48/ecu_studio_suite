#pragma once
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QTextEdit>
#include <QThread>
#include <memory>
#include "mpps/MppsDevice.hpp"

namespace ecu_studio {

class MppsPanel : public QWidget {
    Q_OBJECT
public:
    explicit MppsPanel(QWidget* parent = nullptr);

    mpps::MppsDevice* device() const { return m_device.get(); }

public slots:
    void scanDevices();
    void connectDevice();
    void disconnectDevice();
    void readRom();
    void writeRom(const QByteArray& rom = {});

signals:
    void deviceStatusChanged(const QString& status);
    void romReadComplete(QByteArray rom);
    void operationFailed(const QString& error);
    void progressUpdated(int pct, const QString& msg);

private:
    void buildUi();
    void log(const QString& msg, bool error = false);
    void setOperating(bool on);

    QComboBox*    m_deviceCombo{nullptr};
    QPushButton*  m_scanBtn{nullptr};
    QPushButton*  m_connectBtn{nullptr};
    QLabel*       m_statusDot{nullptr};
    QLabel*       m_statusText{nullptr};
    QLabel*       m_ecuLabel{nullptr};
    QComboBox*    m_protocolCombo{nullptr};
    QPushButton*  m_readBtn{nullptr};
    QPushButton*  m_writeBtn{nullptr};
    QProgressBar* m_progress{nullptr};
    QLabel*       m_progressLabel{nullptr};
    QTextEdit*    m_log{nullptr};

    std::unique_ptr<mpps::MppsDevice> m_device;
    std::vector<mpps::MppsDeviceInfo> m_devices;
    QByteArray m_pendingWrite;
};

} // namespace ecu_studio
