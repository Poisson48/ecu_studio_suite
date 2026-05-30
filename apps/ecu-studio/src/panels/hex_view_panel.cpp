#include "hex_view_panel.h"
#include <QVBoxLayout>
#include <QLabel>
namespace ecu_studio {
HexViewPanel::HexViewPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    auto* lbl = new QLabel(tr("HexView — en développement"), this);
    lbl->setStyleSheet("color:#7c8fa6; font-size:14px;");
    lbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(lbl);
}
void HexViewPanel::loadRom(const QString& /*path*/) {}
void HexViewPanel::loadRom(const QByteArray& /*data*/) {}
} // namespace ecu_studio
