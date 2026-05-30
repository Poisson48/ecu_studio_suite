#pragma once
#include <QWidget>
namespace ecu_studio {
class ComparePanel : public QWidget {
    Q_OBJECT
public:
    explicit ComparePanel(QWidget* parent = nullptr);
public slots:
    void openComparison();
};
} // namespace ecu_studio
