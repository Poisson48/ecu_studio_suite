#pragma once
#include <QByteArray>
#include <cstdint>
#include <span>

namespace ecu_studio {

// Vue std::span<const uint8_t> sur un QByteArray, sans copie. Centralise le
// reinterpret_cast répété dans tous les panels (Hex, Maps, Checksum, Compare,
// AutoMods, MPPS) et la génération de rapport.
inline std::span<const uint8_t> constByteSpan(const QByteArray& a) {
    return { reinterpret_cast<const uint8_t*>(a.constData()),
             static_cast<std::size_t>(a.size()) };
}

// Vue mutable sur un QByteArray (pour patching in-place).
inline std::span<uint8_t> mutByteSpan(QByteArray& a) {
    return { reinterpret_cast<uint8_t*>(a.data()),
             static_cast<std::size_t>(a.size()) };
}

} // namespace ecu_studio
