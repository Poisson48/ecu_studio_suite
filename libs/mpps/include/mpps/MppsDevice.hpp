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
// TODO_REVERSE : valider depuis mpps_protocol_capture.log
constexpr uint8_t STX             = 0x68;
constexpr uint8_t CMD_HANDSHAKE   = 0x01;
constexpr uint8_t CMD_ECU_IDENT   = 0x02;
constexpr uint8_t CMD_READ_BLOCK  = 0x10;
constexpr uint8_t CMD_WRITE_BLOCK = 0x11;
constexpr uint8_t CMD_ERASE       = 0x20;
constexpr uint8_t CMD_CHECKSUM    = 0x30;
constexpr uint8_t CMD_ENTER_PROG  = 0x40;
constexpr uint8_t CMD_EXIT_PROG   = 0x41;
constexpr uint8_t CMD_KLINE_INIT  = 0x50;
constexpr uint8_t CMD_CAN_INIT    = 0x51;
constexpr uint8_t CMD_ABORT       = 0xFF;

constexpr uint32_t MAX_BLOCK_SIZE      = 256;
constexpr uint32_t DEFAULT_TIMEOUT_MS  = 1000;
constexpr uint32_t FLASH_TIMEOUT_MS    = 5000;
} // namespace protocol

} // namespace mpps
