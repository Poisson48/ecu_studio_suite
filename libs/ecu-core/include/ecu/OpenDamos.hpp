#pragma once
#include <QString>
#include <QByteArray>
#include <optional>
#include <cstdint>

namespace ecu {

struct DamosFingerprint {
    QString ecuId;
    QString variant;
    uint32_t checksumOffset = 0;
};

class OpenDamos {
public:
    std::optional<DamosFingerprint> fingerprint(const QByteArray& rom);
    bool relocate(QByteArray& rom, uint32_t baseOffset);
};

} // namespace ecu
