#pragma once
#include <QWidget>
namespace ecu_studio {
class ChecksumPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChecksumPanel(QWidget* parent = nullptr);
public slots:
    void runCorrection();
};
} // namespace ecu_studio
