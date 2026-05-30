#include "compare_panel.h"
#include <QVBoxLayout>
#include <QLabel>
namespace ecu_studio {
ComparePanel::ComparePanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    auto* lbl = new QLabel(tr("Compare — en développement"), this);
    lbl->setStyleSheet("color:#7c8fa6; font-size:14px;");
    lbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(lbl);
}
void ComparePanel::openComparison() {}
} // namespace ecu_studio
