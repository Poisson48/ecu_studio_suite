#include "mpps/MppsDevice.hpp"
#include <libusb-1.0/libusb.h>
#include <array>
#include <thread>
#include <chrono>
#include <format>
#include <algorithm>

#ifdef ECU_MPPS_PROTOCOL_LOG
#include <fstream>
static std::ofstream s_protoLog("mpps_protocol.log", std::ios::app);
#define PROTO_LOG(msg) if(s_protoLog.is_open()) s_protoLog << msg << "\n"
#else
#define PROTO_LOG(msg)
#endif

namespace mpps {

static constexpr std::array<std::pair<uint16_t,uint16_t>, 6> KNOWN_DEVICES {{
    {0x0403, 0x6001},  // FTDI FT232BM/RL
    {0x0403, 0x6010},  // FTDI FT2232H
    {0x0403, 0x6015},  // FTDI FTX
    {0x1A86, 0x7523},  // WCH CH340
    {0x1A86, 0x5523},  // WCH CH341
    {0x10C4, 0xEA60},  // SiLabs CP2102
}};

static std::string chipTypeStr(uint16_t vid, uint16_t pid) {
    if (vid == 0x0403) {
        if (pid == 0x6010) return "FTDI FT2232H";
        if (pid == 0x6015) return "FTDI FTX";
        return "FTDI FT232R";
    }
    if (vid == 0x1A86) return pid == 0x7523 ? "WCH CH340" : "WCH CH341";
    if (vid == 0x10C4) return "SiLabs CP2102";
    return "Unknown";
}

static std::string hexDump(const std::vector<uint8_t>& v) {
    std::string s;
    for (auto b : v) s += std::format("{:02X} ", b);
    return s;
}

class MppsDeviceLibUsb : public MppsDevice {
public:
    explicit MppsDeviceLibUsb(MppsDeviceInfo info) : m_info(std::move(info)) {}
    ~MppsDeviceLibUsb() override { disconnect(); }

    MppsResult<MppsDeviceInfo> connect() override {
        if (m_connected) return m_info;

        if (libusb_init(&m_ctx) < 0) return std::unexpected(MppsError::UsbError);

        libusb_device** list = nullptr;
        ssize_t cnt = libusb_get_device_list(m_ctx, &list);
        libusb_device* found = nullptr;

        for (ssize_t i = 0; i < cnt; i++) {
            libusb_device_descriptor desc;
            libusb_get_device_descriptor(list[i], &desc);
            if (desc.idVendor == m_info.vid && desc.idProduct == m_info.pid) {
                found = libusb_ref_device(list[i]);
                break;
            }
        }
        libusb_free_device_list(list, 1);

        if (!found) return std::unexpected(MppsError::DeviceNotFound);

        int r = libusb_open(found, &m_handle);
        libusb_unref_device(found);
        if (r < 0) return std::unexpected(MppsError::UsbError);

#ifdef ECU_PLATFORM_LINUX
        if (libusb_kernel_driver_active(m_handle, 0) == 1) {
            if (libusb_detach_kernel_driver(m_handle, 0) < 0) {
                libusb_close(m_handle);
                return std::unexpected(MppsError::DriverNotReady);
            }
            m_driverDetached = true;
        }
#endif

        if (libusb_claim_interface(m_handle, 0) < 0) {
            libusb_close(m_handle);
            return std::unexpected(MppsError::UsbError);
        }

        // TODO_REVERSE: confirmer baud rate initial depuis mpps_protocol_capture.log
        configureFTDI(38400);

        auto result = sendCommand(protocol::CMD_HANDSHAKE, {});
        if (!result) return std::unexpected(MppsError::EcuNotResponding);

        if (!result->empty())
            m_info.firmwareVersion = std::string(result->begin(), result->end());
        m_info.isMpps = true;
        m_connected   = true;

        PROTO_LOG("[connect] OK fw=" + m_info.firmwareVersion);
        return m_info;
    }

    void disconnect() override {
        if (!m_connected) return;
        sendCommand(protocol::CMD_ABORT, {});
        libusb_release_interface(m_handle, 0);
#ifdef ECU_PLATFORM_LINUX
        if (m_driverDetached) libusb_attach_kernel_driver(m_handle, 0);
#endif
        libusb_close(m_handle);
        libusb_exit(m_ctx);
        m_handle    = nullptr;
        m_ctx       = nullptr;
        m_connected = false;
    }

    bool isConnected() const override { return m_connected; }

    MppsResult<std::string> identifyEcu() override {
        auto r = sendCommand(protocol::CMD_ECU_IDENT, {});
        if (!r) return std::unexpected(r.error());
        return std::string(r->begin(), r->end());
    }

    MppsResult<void> enterProgMode() override {
        auto r = sendCommand(protocol::CMD_ENTER_PROG, {});
        if (!r) return std::unexpected(r.error());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return {};
    }

    MppsResult<void> exitProgMode() override {
        auto r = sendCommand(protocol::CMD_EXIT_PROG, {});
        if (!r) return std::unexpected(r.error());
        return {};
    }

    MppsResult<void> setProtocol(PhysicalProtocol p, uint32_t bitrate) override {
        uint8_t cmd = (p == PhysicalProtocol::KLine) ? protocol::CMD_KLINE_INIT
                                                      : protocol::CMD_CAN_INIT;
        std::vector<uint8_t> payload = {
            uint8_t((bitrate >> 8) & 0xFF),
            uint8_t( bitrate       & 0xFF),
        };
        auto r = sendCommand(cmd, payload);
        if (!r) return std::unexpected(r.error());
        return {};
    }

    MppsResult<std::vector<uint8_t>> readBlock(
        uint32_t address, uint32_t length, ProgressCb cb) override
    {
        std::vector<uint8_t> result;
        result.reserve(length);

        for (uint32_t off = 0; off < length; off += protocol::MAX_BLOCK_SIZE) {
            uint32_t chunk = std::min(protocol::MAX_BLOCK_SIZE, length - off);
            uint32_t addr  = address + off;
            std::vector<uint8_t> payload = {
                uint8_t((addr  >> 16) & 0xFF), uint8_t((addr  >>  8) & 0xFF), uint8_t(addr  & 0xFF),
                uint8_t((chunk >>  8) & 0xFF), uint8_t(chunk  & 0xFF),
            };
            auto r = sendCommand(protocol::CMD_READ_BLOCK, payload, protocol::DEFAULT_TIMEOUT_MS);
            if (!r) return std::unexpected(r.error());
            result.insert(result.end(), r->begin(), r->end());
            if (cb) cb(off + chunk, length, std::format("Lecture 0x{:06X}", addr));
        }
        return result;
    }

    MppsResult<void> writeBlock(
        uint32_t address, std::span<const uint8_t> data, ProgressCb cb) override
    {
        for (uint32_t off = 0; off < data.size(); off += protocol::MAX_BLOCK_SIZE) {
            uint32_t chunk = std::min((uint32_t)protocol::MAX_BLOCK_SIZE,
                                      (uint32_t)data.size() - off);
            uint32_t addr = address + off;
            std::vector<uint8_t> payload = {
                uint8_t((addr  >> 16) & 0xFF), uint8_t((addr >>  8) & 0xFF), uint8_t(addr  & 0xFF),
                uint8_t((chunk >>  8) & 0xFF), uint8_t(chunk & 0xFF),
            };
            payload.insert(payload.end(), data.begin() + off, data.begin() + off + chunk);
            auto r = sendCommand(protocol::CMD_WRITE_BLOCK, payload, protocol::FLASH_TIMEOUT_MS);
            if (!r) return std::unexpected(r.error());
            if (cb) cb(off + chunk, data.size(), std::format("Écriture 0x{:06X}", addr));
        }
        return {};
    }

    MppsResult<void> eraseSector(uint32_t address, uint32_t size) override {
        std::vector<uint8_t> payload = {
            uint8_t((address >> 16) & 0xFF), uint8_t((address >> 8) & 0xFF), uint8_t(address & 0xFF),
            uint8_t((size    >>  8) & 0xFF), uint8_t(size    & 0xFF),
        };
        auto r = sendCommand(protocol::CMD_ERASE, payload, protocol::FLASH_TIMEOUT_MS);
        if (!r) return std::unexpected(r.error());
        return {};
    }

    MppsResult<uint32_t> hardwareChecksum(uint32_t start, uint32_t end) override {
        std::vector<uint8_t> payload = {
            uint8_t((start >> 16) & 0xFF), uint8_t((start >> 8) & 0xFF), uint8_t(start & 0xFF),
            uint8_t((end   >> 16) & 0xFF), uint8_t((end   >> 8) & 0xFF), uint8_t(end   & 0xFF),
        };
        auto r = sendCommand(protocol::CMD_CHECKSUM, payload);
        if (!r) return std::unexpected(r.error());
        if (r->size() < 4) return std::unexpected(MppsError::ProtocolError);
        uint32_t csum = ((*r)[0] << 24) | ((*r)[1] << 16) | ((*r)[2] << 8) | (*r)[3];
        return csum;
    }

    // ── enumerate ─────────────────────────────────────────────────────────────
    static std::vector<MppsDeviceInfo> enumerateImpl() {
        std::vector<MppsDeviceInfo> out;
        libusb_context* ctx = nullptr;
        libusb_init(&ctx);
        libusb_device** list = nullptr;
        ssize_t cnt = libusb_get_device_list(ctx, &list);
        for (ssize_t i = 0; i < cnt; i++) {
            libusb_device_descriptor d;
            libusb_get_device_descriptor(list[i], &d);
            for (auto [vid, pid] : KNOWN_DEVICES) {
                if (d.idVendor == vid && d.idProduct == pid) {
                    MppsDeviceInfo info;
                    info.vid      = vid;
                    info.pid      = pid;
                    info.chipType = chipTypeStr(vid, pid);
                    info.path     = std::format("usb:{:04x}:{:04x}:{}", vid, pid, i);
                    out.push_back(info);
                    break;
                }
            }
        }
        libusb_free_device_list(list, 1);
        libusb_exit(ctx);
        return out;
    }

private:
    // TODO_REVERSE: confirmer EP endpoints depuis le log
    static constexpr uint8_t EP_OUT = 0x02;
    static constexpr uint8_t EP_IN  = 0x81;

    void configureFTDI(uint32_t /*baudrate*/) {
        // TODO_REVERSE: émettre le bon control transfer depuis le log FT_SetBaudRate
    }

    MppsResult<std::vector<uint8_t>> sendCommand(
        uint8_t cmd, const std::vector<uint8_t>& payload,
        int timeoutMs = protocol::DEFAULT_TIMEOUT_MS)
    {
        // TODO_REVERSE: valider structure trame depuis parse_capture.py
        std::vector<uint8_t> frame;
        frame.push_back(protocol::STX);
        frame.push_back(static_cast<uint8_t>(payload.size() + 1));
        frame.push_back(cmd);
        frame.insert(frame.end(), payload.begin(), payload.end());
        uint8_t chk = 0;
        for (auto b : frame) chk ^= b;
        frame.push_back(chk);

        PROTO_LOG("TX: " + hexDump(frame));

        int transferred = 0;
        int r = libusb_bulk_transfer(m_handle, EP_OUT,
                    frame.data(), (int)frame.size(), &transferred, timeoutMs);
        if (r != LIBUSB_SUCCESS) return std::unexpected(MppsError::UsbError);

        std::vector<uint8_t> resp(4096);
        r = libusb_bulk_transfer(m_handle, EP_IN,
                resp.data(), (int)resp.size(), &transferred, timeoutMs);
        if (r != LIBUSB_SUCCESS) return std::unexpected(MppsError::Timeout);
        resp.resize(transferred);

        PROTO_LOG("RX: " + hexDump(resp));

        if (resp.empty()) return std::unexpected(MppsError::ProtocolError);
        if (resp.size() <= 3) return std::vector<uint8_t>{};
        return std::vector<uint8_t>(resp.begin() + 3, resp.end() - 1);
    }

    MppsDeviceInfo       m_info;
    libusb_context*      m_ctx           = nullptr;
    libusb_device_handle* m_handle       = nullptr;
    bool                 m_connected     = false;
    bool                 m_driverDetached = false;
};

// ── Factory ──────────────────────────────────────────────────────────────────

std::vector<MppsDeviceInfo> MppsDevice::enumerate() {
#ifdef ECU_MPPS_SIMULATION
    return {{ .path="sim://", .chipType="SIMULATION", .isMpps=true }};
#else
    return MppsDeviceLibUsb::enumerateImpl();
#endif
}

std::unique_ptr<MppsDevice> MppsDevice::open(const MppsDeviceInfo& info) {
#ifdef ECU_MPPS_SIMULATION
    (void)info;
    return openSimulation();
#else
    return std::make_unique<MppsDeviceLibUsb>(info);
#endif
}

} // namespace mpps
