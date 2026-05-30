#pragma once
#include <QWidget>
namespace ecu_studio {
class GitPanel : public QWidget {
    Q_OBJECT
public:
    explicit GitPanel(QWidget* parent = nullptr);
};
} // namespace ecu_studio
