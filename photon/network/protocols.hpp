#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <stop_token>
#include <string>
#include <variant>
#include <vector>

#include "../parse/arena.hpp"
#include "../parse/spmc.hpp"
#include "canp.h"

#if defined(LINUX) || defined(APPLE) || defined(__APPLE__)

enum Protocol {
  Protocol_None = 0x00,
  Protocol_TCP = 0x01,
  Protocol_UDP = 0x02,
  Protocol_UART = 0x03,
  Protocol_PCAN = 0x04,
  Protocol_BLE = 0x05,
  Protocol_WLAN = 0x06,
  Protocol_MaxValue = 0xFF
};

struct Quit {};

struct TCPConfig {
  uint16_t port = 9000;
  char ip[256] = "127.0.0.1";
  bool useAwsExtendedTelemetryDBC = false;
  bool recordOnConnect = false;
};

struct UDPConfig {
  uint16_t port = 9000;
  char ip[256] = "127.0.0.1";
  char subscribeMessage[1024] = "subscribe";
};

struct UARTConfig {
  uint32_t baudRate = 115200;
  char device[1024] = "/dev/ttyUSB0";
};

struct PCANConfig {
  uint32_t bitrateKbps = 250;
  uint32_t prescaler = 1;
  uint32_t btr0 = 0x00;
  uint32_t btr1 = 0x1C;
  bool useBtr = false;
  bool listenOnly = false;
  bool busoffReset = false;
  float samplePointPercent = 87.5f;
  char channel[1024] = "can0";
};

struct DashboardConfig {
  uint16_t port = 39002;
  char ip[256] = "192.168.4.1";
};

struct BLEConfig {};

struct WLANConfig {};
#endif

#ifdef _WIN32
struct TCPConfig {
  uint16_t port = 9000;
  char ip[256] = "127.0.0.1";
  bool useAwsExtendedTelemetryDBC = false;
  bool recordOnConnect = false;
};

struct DashboardConfig {
  uint16_t port = 39002;
  char ip[256] = "192.168.4.1";
};

struct Quit {};

struct UDPConfig {
  uint16_t port = 9000;
  char ip[256] = "127.0.0.1";
  char subscribeMessage[1024] = "subscribe";
};

struct UARTConfig {
  uint32_t baudRate = 115200;
  char device[1024] = "COM1";
};

struct PCANConfig {
  uint32_t bitrateKbps = 250;
  uint32_t prescaler = 1;
  uint32_t btr0 = 0x00;
  uint32_t btr1 = 0x1C;
  bool useBtr = false;
  bool listenOnly = false;
  bool busoffReset = false;
  float samplePointPercent = 87.5f;
  char channel[1024] = "PCAN_USBBUS1";
};

struct BLEConfig {};

struct WLANConfig {};
#endif

struct ProtocolError {
  std::string error{};
};

struct ProtocolMessage {
  std::string message{};
};

struct ProtocolDeviceList {
  std::vector<std::string> devices;
};
using ProtocolTransmitVariant = std::variant<TCPConfig, UDPConfig, UARTConfig, PCANConfig,
                                             DashboardConfig, BLEConfig, WLANConfig, Quit>;
using ProtocolReceiveVariant = std::variant<ProtocolError, ProtocolMessage, ProtocolDeviceList>;

struct CANSignalValue {
  std::string signalName{};
  double physicalValue = 0.0;
};

// A complete DBC-backed frame update.  `allowUnseenFrame` is reserved for
// deliberately configured test presets; ordinary List edits retain the safer
// requirement that a multi-signal frame has first been observed on the bus.
struct CANFrameWrite {
  std::string messageName{};
  std::vector<CANSignalValue> values{};
  bool allowUnseenFrame = false;
};

struct CANFrameEvent {
  uint32_t id = 0;
  uint8_t dlc = 0;
  std::array<uint8_t, 8> data{};
  uint64_t timestampMs = 0;
  bool transmitted = false;
};

namespace CANCodec {
bool decodeSignal(const uint8_t data[8], uint8_t dlc, const Signal& signal, double& value);
bool encodeSignal(uint8_t data[8], uint8_t dlc, const Signal& signal, double physicalValue,
                  std::string& error);
}  // namespace CANCodec

struct alignas(64) TimelineCursorMailbox {
  static constexpr uint64_t timestampMask = (uint64_t{1} << 48) - 1;
  static constexpr uint64_t pack(uint16_t command, uint64_t timestamp) {
    return static_cast<uint64_t>(command) << 48 | (timestamp & timestampMask);
  }
  static constexpr uint16_t command(uint64_t value) { return static_cast<uint16_t>(value >> 48); }
  static constexpr uint64_t timestamp(uint64_t value) { return value & timestampMask; }

  std::atomic<uint64_t> request{pack(CANP_TIMELINE_LIVE, 0)};
  std::atomic<uint64_t> sequence{};
  std::atomic<uint64_t> response{pack(CANP_TIMELINE_LIVE, 0)};
  std::atomic<uint64_t> statusSequence{};
  std::atomic<uint64_t> latestTimestampMs{};
};

static_assert(sizeof(TimelineCursorMailbox) == 64);

struct Protocols {
  static void TCP(std::stop_token stoken, SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer,
                  TCPConfig config, Arena& arena, TimelineCursorMailbox& timelineCursor);
  static void PCAN(std::stop_token stoken, SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer,
                   SPMCQueue<CANFrameWrite, 64>& writeBuffer,
                   SPMCQueue<CANFrameEvent, 512>& frameEvents, PCANConfig config, Arena& arena);
  static void Dashboard(std::stop_token stoken, SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer,
                        SPMCQueue<CANFrameWrite, 64>& writeBuffer,
                        SPMCQueue<CANFrameEvent, 512>& frameEvents, DashboardConfig config,
                        Arena& arena, std::atomic<bool>& armRequested,
                        std::atomic<bool>& transmitAvailable, std::atomic<bool>& controlsArmed);
  static void discoverDashboards(std::stop_token stoken,
                                 SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer);
};
