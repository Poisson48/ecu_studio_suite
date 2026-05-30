#include "ecu/OpenDamos.hpp"

namespace ecu {

std::optional<DamosFingerprint> OpenDamos::fingerprint(const QByteArray& /*rom*/) {
    // TODO: implémenter fingerprinting DAMOS
    return std::nullopt;
}

bool OpenDamos::relocate(QByteArray& /*rom*/, uint32_t /*baseOffset*/) {
    // TODO: implémenter relocation DAMOS
    return false;
}

} // namespace ecu
