#include "mpps/MppsDevice.hpp"
#include <thread>
#include <chrono>
#include <algorithm>

namespace mpps {

class MppsDeviceSim : public MppsDevice {
public:
    MppsResult<MppsDeviceInfo> connect() override {
        m_connected = true;
        m_rom.assign(0x200000, 0xFF);
        for (size_t i = 0x1C0000; i < 0x200000; i += 4)
            m_rom[i] = static_cast<uint8_t>(i & 0xFF);

        MppsDeviceInfo info;
        info.path            = "sim://";
        info.chipType        = "SIMULATION";
        info.firmwareVersion = "SIM-1.0";
        info.isMpps          = true;
        m_info = info;
        return info;
    }

    void disconnect() override { m_connected = false; }
    bool isConnected() const override { return m_connected; }

    MppsResult<std::string> identifyEcu() override { return "EDC16C34_SIM"; }
    MppsResult<void> enterProgMode() override { return {}; }
    MppsResult<void> exitProgMode()  override { return {}; }
    MppsResult<void> setProtocol(PhysicalProtocol, uint32_t) override { return {}; }

    MppsResult<uint32_t> hardwareChecksum(uint32_t, uint32_t) override {
        return 0xD110DD00u;
    }

    MppsResult<void> eraseSector(uint32_t addr, uint32_t size) override {
        if (addr + size > m_rom.size()) return std::unexpected(MppsError::UsbError);
        std::fill(m_rom.begin() + addr, m_rom.begin() + addr + size, 0xFF);
        return {};
    }

    MppsResult<std::vector<uint8_t>> readBlock(
        uint32_t address, uint32_t length, ProgressCb cb) override
    {
        if (address + length > m_rom.size())
            return std::unexpected(MppsError::UsbError);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (cb) cb(length, length, "Simulation lecture");
        return std::vector<uint8_t>(m_rom.begin() + address,
                                    m_rom.begin() + address + length);
    }

    MppsResult<void> writeBlock(
        uint32_t address, std::span<const uint8_t> data, ProgressCb cb) override
    {
        if (address + data.size() > m_rom.size())
            return std::unexpected(MppsError::UsbError);
        std::copy(data.begin(), data.end(), m_rom.begin() + address);
        if (cb) cb(data.size(), data.size(), "Simulation écriture");
        return {};
    }

private:
    bool m_connected = false;
    std::vector<uint8_t> m_rom;
    MppsDeviceInfo m_info;
};

// ── Factory (simulation mode) ─────────────────────────────────────────────────

std::unique_ptr<MppsDevice> MppsDevice::openSimulation() {
    return std::make_unique<MppsDeviceSim>();
}

#ifdef ECU_MPPS_SIMULATION
std::vector<MppsDeviceInfo> MppsDevice::enumerate() {
    MppsDeviceInfo info;
    info.path     = "sim://";
    info.chipType = "SIMULATION";
    info.isMpps   = true;
    return { info };
}

std::unique_ptr<MppsDevice> MppsDevice::open(const MppsDeviceInfo&) {
    return openSimulation();
}
#endif

} // namespace mpps
