#pragma once
#include <QWidget>
#include <QString>

namespace ecu_studio {

class HexViewPanel : public QWidget {
    Q_OBJECT
public:
    explicit HexViewPanel(QWidget* parent = nullptr);
    void loadRom(const QString& path);
    void loadRom(const QByteArray& data);
};

} // namespace ecu_studio
