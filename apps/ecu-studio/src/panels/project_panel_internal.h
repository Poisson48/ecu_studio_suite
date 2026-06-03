#pragma once
// Rôles Qt::UserRole partagés par les TU de ProjectPanel (id projet, ECU, slug slot).
#include <QObject>

namespace ecu_studio {

constexpr int IdRole   = Qt::UserRole + 1;
constexpr int EcuRole  = Qt::UserRole + 2;
constexpr int SlugRole = Qt::UserRole + 3;

} // namespace ecu_studio
