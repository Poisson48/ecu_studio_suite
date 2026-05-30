#include "automods_panel.h"
#include <QVBoxLayout>
#include <QLabel>
namespace ecu_studio {
AutoModsPanel::AutoModsPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    auto* lbl = new QLabel(tr("AutoMods — en développement"), this);
    lbl->setStyleSheet("color:#7c8fa6; font-size:14px;");
    lbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(lbl);
}
} // namespace ecu_studio
