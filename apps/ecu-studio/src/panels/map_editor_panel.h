#pragma once
#include <QWidget>
namespace ecu_studio {
class MapEditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit MapEditorPanel(QWidget* parent = nullptr);
public slots:
    void runMapFinder();
};
} // namespace ecu_studio
