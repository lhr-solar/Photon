#pragma once
#include <atomic>
#include <cstdint>
#include <stop_token>
#include <string>
#include <variant>
#include <vector>

#include "../parse/arena.hpp"
#include "../parse/spmc.hpp"

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
  uint8_t tag = 0;
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
  uint32_t bitrateKbps = 500;
  uint32_t prescaler = 1;
  uint32_t btr0 = 0x00;
  uint32_t btr1 = 0x1C;
  bool useBtr = false;
  bool listenOnly = false;
  bool busoffReset = false;
  float samplePointPercent = 87.5f;
  char channel[1024] = "can0";
};

struct BLEConfig {};

struct WLANConfig {};
#endif

#ifdef _WIN32
struct TCPConfig {
  uint16_t port = 9000;
  char ip[256] = "127.0.0.1";
  uint8_t tag = 0;
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
  uint32_t bitrateKbps = 500;
  uint32_t prescaler = 1;
  uint32_t btr0 = 0x00;
  uint32_t btr1 = 0x1C;
  bool useBtr = false;
  bool listenOnly = false;
  bool busoffReset = false;
  float samplePointPercent = 87.5f;
  char channel[1024] = "can0";
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
using ProtocolTransmitVariant =
    std::variant<TCPConfig, UDPConfig, UARTConfig, PCANConfig, BLEConfig, WLANConfig, Quit>;
using ProtocolReceiveVariant = std::variant<ProtocolError, ProtocolMessage, ProtocolDeviceList>;

struct alignas(64) TimelineCursorMailbox {
  std::atomic<uint64_t> timestampMs{};
  std::atomic<uint64_t> sequence{};
};

static_assert(sizeof(TimelineCursorMailbox) == 64);

struct Protocols {
  static void TCP(std::stop_token stoken, SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer,
                  TCPConfig config, Arena& arena, TimelineCursorMailbox& timelineCursor);
};
