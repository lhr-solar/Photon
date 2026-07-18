#pragma once

#include <array>
#include <cstdint>

// Private Photon Dashboard relay wire format.  The payload of telemetry and
// write messages is a DashboardBatch followed by `count` CANP packets.
namespace DashboardLink {

constexpr uint32_t kMagic = 0x50484431u;  // "PHD1"
constexpr uint16_t kVersion = 1;
constexpr uint16_t kDiscoveryPort = 39001;
constexpr uint16_t kStreamPort = 39002;
constexpr uint32_t kMaxPayload = 64u * 29u + 16u;

enum class MessageType : uint16_t {
  Hello = 1,
  Telemetry = 2,
  ArmRequest = 3,
  ArmRelease = 4,
  ArmGranted = 5,
  ArmDenied = 6,
  Status = 7,
  CanWrite = 8,
  Heartbeat = 9,
};

enum StatusFlags : uint8_t {
  RemoteWritesEnabled = 1 << 0,
  ControllerLeaseHeld = 1 << 1,
};

#pragma pack(push, 1)
struct Header {
  uint32_t magic;
  uint16_t version;
  uint16_t type;
  uint32_t payloadBytes;
  uint32_t sequence;
};

struct BatchHeader {
  uint64_t timestampMs;
  uint32_t sequence;
  uint16_t count;
  uint16_t reserved;
};

struct DiscoveryRequest {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
};

struct DiscoveryResponse {
  uint32_t magic;
  uint16_t version;
  uint16_t streamPort;
  uint8_t statusFlags;
  std::array<char, 47> name;
};
#pragma pack(pop)

static_assert(sizeof(Header) == 16);
static_assert(sizeof(BatchHeader) == 16);
static_assert(sizeof(DiscoveryRequest) == 8);
static_assert(sizeof(DiscoveryResponse) == 56);

}  // namespace DashboardLink
