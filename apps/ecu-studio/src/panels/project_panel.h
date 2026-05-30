#pragma once
#include <QWidget>
namespace ecu_studio {
class ProjectPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProjectPanel(QWidget* parent = nullptr);
public slots:
    void newProject();
    void openProject();
};
} // namespace ecu_studio
