#include "mpps/MppsDevice.hpp"
#include <format>

namespace mpps {

std::string to_string(MppsError e) {
    switch (e) {
    case MppsError::DeviceNotFound:   return "Périphérique MPPS non trouvé";
    case MppsError::DriverNotReady:   return "Driver USB non prêt (ftdi_sio attaché ?)";
    case MppsError::UsbError:         return "Erreur USB";
    case MppsError::Timeout:          return "Timeout";
    case MppsError::ProtocolError:    return "Erreur protocole MPPS";
    case MppsError::ChecksumMismatch: return "Checksum invalide";
    case MppsError::EcuNotResponding: return "ECU ne répond pas";
    case MppsError::WriteProtected:   return "ECU protégé en écriture";
    case MppsError::NotImplemented:   return "Non implémenté";
    case MppsError::SimulationMode:   return "Mode simulation";
    }
    return "Erreur inconnue";
}

MppsResult<std::vector<uint8_t>> MppsDevice::readFullRom(uint32_t romSize, ProgressCb cb) {
    auto r = enterProgMode();
    if (!r) return std::unexpected(r.error());

    auto data = readBlock(0, romSize, cb);

    exitProgMode();
    return data;
}

MppsResult<void> MppsDevice::writeFullRom(std::span<const uint8_t> rom, ProgressCb cb) {
    if (rom.empty()) return std::unexpected(MppsError::ProtocolError);

    auto r = enterProgMode();
    if (!r) return std::unexpected(r.error());

    auto wr = writeBlock(0, rom, cb);
    exitProgMode();
    return wr;
}

} // namespace mpps
