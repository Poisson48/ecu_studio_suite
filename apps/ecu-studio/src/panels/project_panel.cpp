#include "project_panel.h"
#include <QVBoxLayout>
#include <QLabel>
namespace ecu_studio {
ProjectPanel::ProjectPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    auto* lbl = new QLabel(tr("Project — en développement"), this);
    lbl->setStyleSheet("color:#7c8fa6; font-size:14px;");
    lbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(lbl);
}
void ProjectPanel::newProject() {}
void ProjectPanel::openProject() {}
} // namespace ecu_studio
