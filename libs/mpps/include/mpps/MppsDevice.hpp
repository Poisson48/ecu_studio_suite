#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <functional>
#include <expected>
#include <string>
#include <span>

namespace mpps {

enum class MppsError {
    DeviceNotFound,
    DriverNotReady,
    UsbError,
    Timeout,
    ProtocolError,
    ChecksumMismatch,
    EcuNotResponding,
    WriteProtected,
    NotImplemented,
    SimulationMode,
};

std::string to_string(MppsError e);

template<typename T>
using MppsResult = std::expected<T, MppsError>;

struct MppsDeviceInfo {
    std::string path;
    std::string serial;
    std::string chipType;
    uint16_t    vid = 0;
    uint16_t    pid = 0;
    std::string firmwareVersion;
    bool        isMpps = false;
};

using ProgressCb = std::function<void(uint32_t done, uint32_t total, const std::string& msg)>;

class MppsDevice {
public:
    virtual ~MppsDevice() = default;

    static std::vector<MppsDeviceInfo> enumerate();
    static std::unique_ptr<MppsDevice> open(const MppsDeviceInfo& info);
    static std::unique_ptr<MppsDevice> openSimulation();

    virtual MppsResult<MppsDeviceInfo> connect()    = 0;
    virtual void                       disconnect()  = 0;
    virtual bool                       isConnected() const = 0;

    virtual MppsResult<std::string>  identifyEcu()   = 0;
    virtual MppsResult<void>         enterProgMode()  = 0;
    virtual MppsResult<void>         exitProgMode()   = 0;

    virtual MppsResult<std::vector<uint8_t>> readBlock(
        uint32_t address, uint32_t length, ProgressCb cb = nullptr) = 0;

    virtual MppsResult<void> writeBlock(
        uint32_t address, std::span<const uint8_t> data,
        ProgressCb cb = nullptr) = 0;

    virtual MppsResult<void> eraseSector(uint32_t address, uint32_t size) = 0;

    MppsResult<std::vector<uint8_t>> readFullRom(uint32_t romSize, ProgressCb cb = nullptr);
    MppsResult<void> writeFullRom(std::span<const uint8_t> rom, ProgressCb cb = nullptr);

    enum class PhysicalProtocol { KLine, Can, Auto };
    virtual MppsResult<void> setProtocol(
        PhysicalProtocol p, uint32_t bitrate = 500000) = 0;

    virtual MppsResult<uint32_t> hardwareChecksum(uint32_t start, uint32_t end) = 0;
};

namespace protocol {
// ─────────────────────────────────────────────────────────────────────────────
// TODO_REVERSE — the PC↔dongle wire framing below is UNCONFIRMED.
//
// Mpps.exe (and MultiBoot/Tricore Boot) are PACKED C++Builder binaries: a raw
// byte search of all three for "ftd2xx.dll" / "FT_*" / command tables returns
// ZERO hits — every protocol literal is compressed in the .didata/.data sections
// and only materialises after the in-process unpacker runs at load time. With no
// wine / no execution available, the frame builder cannot be recovered by static
// analysis. See docs/mpps-reverse.md §2.3 / §5.
//
// Every constant in THIS block is a HYPOTHESIS with NO binary evidence. Do not
// rely on it for real hardware until confirmed by mpps_protocol_capture.log via
// tools/reverse/CAPTURE_SOP.md + parse_capture.py.
// ─────────────────────────────────────────────────────────────────────────────
constexpr uint8_t STX             = 0x68;  // HYPOTHESIS (KWP2000 fmt byte); confirm
constexpr uint8_t CMD_HANDSHAKE   = 0x01;  // HYPOTHESIS — no evidence
constexpr uint8_t CMD_ECU_IDENT   = 0x02;  // HYPOTHESIS — no evidence
constexpr uint8_t CMD_READ_BLOCK  = 0x10;  // HYPOTHESIS — no evidence
constexpr uint8_t CMD_WRITE_BLOCK = 0x11;  // HYPOTHESIS — no evidence
constexpr uint8_t CMD_ERASE       = 0x20;  // HYPOTHESIS — no evidence
constexpr uint8_t CMD_CHECKSUM    = 0x30;  // HYPOTHESIS — no evidence
constexpr uint8_t CMD_ENTER_PROG  = 0x40;  // HYPOTHESIS — no evidence
constexpr uint8_t CMD_EXIT_PROG   = 0x41;  // HYPOTHESIS — no evidence
constexpr uint8_t CMD_KLINE_INIT  = 0x50;  // HYPOTHESIS — no evidence
constexpr uint8_t CMD_CAN_INIT    = 0x51;  // HYPOTHESIS — no evidence
constexpr uint8_t CMD_ABORT       = 0xFF;  // HYPOTHESIS — no evidence

constexpr uint32_t MAX_BLOCK_SIZE      = 256;   // HYPOTHESIS — confirm from capture
constexpr uint32_t DEFAULT_TIMEOUT_MS  = 1000;
constexpr uint32_t FLASH_TIMEOUT_MS    = 5000;

// FACT: from Mpps.Cfg/Tricore.Cfg — FTDI latency timer is configured to 10 ms,
// and AutoBaud=1 (the dongle auto-negotiates the K-line/CAN baud with the ECU).
// Source: tools/reverse/mpps_extracted/Mpps.Cfg [Latency] Value=10.
constexpr uint8_t  FTDI_LATENCY_MS     = 10;    // FACT (host-side FT_SetLatencyTimer)
} // namespace protocol

} // namespace mpps
