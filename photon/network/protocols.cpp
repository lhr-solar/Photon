#include "protocols.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../parse/arena.hpp"
#include "canp.h"
#include "dashboardLink.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Ws2tcpip.h>
#include <winsock2.h>
using SocketHandle = SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
using SocketHandle = int;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

void publishMessage(SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer, std::string message) {
  txBuffer.write([&](ProtocolReceiveVariant& out) { out = ProtocolMessage{.message = message}; });
}

void publishError(SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer, std::string error) {
  txBuffer.write([&](ProtocolReceiveVariant& out) { out = ProtocolError{.error = error}; });
}

#ifdef _WIN32
bool ensureWinsock(std::string& error) {
  static bool started = false;
  if (started) return true;

  WSADATA wsa{};
  const int result = WSAStartup(MAKEWORD(2, 2), &wsa);
  if (result != 0) {
    error = "WSAStartup failed: " + std::to_string(result);
    return false;
  }

  started = true;
  return true;
}
#endif

std::string socketError(const char* operation) {
  char buffer[256]{};
#ifdef _WIN32
  std::snprintf(buffer, sizeof(buffer), "%s failed: %d", operation, WSAGetLastError());
#else
  std::snprintf(buffer, sizeof(buffer), "%s failed: %s", operation, std::strerror(errno));
#endif
  return buffer;
}

std::string socketError(const char* operation, int errorCode) {
  char buffer[256]{};
#ifdef _WIN32
  std::snprintf(buffer, sizeof(buffer), "%s failed: %d", operation, errorCode);
#else
  std::snprintf(buffer, sizeof(buffer), "%s failed: %s", operation, std::strerror(errorCode));
#endif
  return buffer;
}

std::string canpReadError(int status) {
  switch (status) {
    case CANP_READ_BAD_MAGIC:
      return "CANP read failed: bad magic";
    case CANP_READ_BAD_VERSION:
      return "CANP read failed: unsupported version";
    case CANP_READ_BAD_COUNT:
      return "CANP read failed: invalid batch count";
    default:
      return "CANP read failed: " + std::to_string(status);
  }
}

bool wouldBlock() {
#ifdef _WIN32
  const int error = WSAGetLastError();
  return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EALREADY;
#endif
}

bool setNonBlocking(SocketHandle sock) {
#ifdef _WIN32
  u_long mode = 1;
  return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
  const int flags = fcntl(sock, F_GETFL, 0);
  return flags >= 0 && fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool setBlocking(SocketHandle sock) {
#ifdef _WIN32
  u_long mode = 0;
  return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
  const int flags = fcntl(sock, F_GETFL, 0);
  return flags >= 0 && fcntl(sock, F_SETFL, flags & ~O_NONBLOCK) == 0;
#endif
}

void closeSocket(SocketHandle sock) {
  if (sock == INVALID_SOCKET) return;
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
}

int selectSocketCount(SocketHandle sock) {
#ifdef _WIN32
  (void)sock;
  return 0;
#else
  return static_cast<int>(sock + 1);
#endif
}

bool waitForConnect(SocketHandle sock, std::stop_token stoken, std::string& error) {
  while (!stoken.stop_requested()) {
    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(sock, &writeSet);

    timeval timeout{};
    timeout.tv_usec = 100000;

    const int ready = select(selectSocketCount(sock), nullptr, &writeSet, nullptr, &timeout);
    if (ready == 0) continue;
    if (ready == SOCKET_ERROR) {
      error = socketError("TCP connect select");
      return false;
    }

    int connectError = 0;
    socklen_t connectErrorSize = sizeof(connectError);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&connectError),
                   &connectErrorSize) == SOCKET_ERROR) {
      error = socketError("TCP connect status");
      return false;
    }
    if (connectError != 0) {
      error = socketError("TCP connect", connectError);
      return false;
    }

    return true;
  }

  error = "TCP connect stopped";
  return false;
}

enum class SocketWaitResult { Ready, Stopped, Error };

SocketWaitResult waitForReadable(SocketHandle sock, std::stop_token stoken, std::string& error) {
  while (!stoken.stop_requested()) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(sock, &readSet);

    timeval timeout{};
    timeout.tv_usec = 100000;

    const int ready = select(selectSocketCount(sock), &readSet, nullptr, nullptr, &timeout);
    if (ready == 0) continue;
    if (ready == SOCKET_ERROR) {
      error = socketError("TCP recv select");
      return SocketWaitResult::Error;
    }
    if (FD_ISSET(sock, &readSet)) return SocketWaitResult::Ready;
  }

  return SocketWaitResult::Stopped;
}

bool finishTcpConnect(SocketHandle& sock, std::string& error) {
  if (setBlocking(sock)) return true;

  error = socketError("TCP blocking setup");
  closeSocket(sock);
  sock = INVALID_SOCKET;
  return false;
}

bool connectTcp(SocketHandle& sock, const TCPConfig& config, std::stop_token stoken,
                std::string& error) {
#ifdef _WIN32
  if (!ensureWinsock(error)) return false;
#endif

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == INVALID_SOCKET) {
    error = socketError("TCP socket creation");
    return false;
  }

  if (!setNonBlocking(sock)) {
    error = socketError("TCP non-blocking setup");
    closeSocket(sock);
    sock = INVALID_SOCKET;
    return false;
  }

  sockaddr_in server{};
  server.sin_family = AF_INET;
  server.sin_port = htons(config.port);
  if (inet_pton(AF_INET, config.ip, &server.sin_addr) != 1) {
    error = "invalid TCP IP address: " + std::string(config.ip);
    closeSocket(sock);
    sock = INVALID_SOCKET;
    return false;
  }

  if (connect(sock, reinterpret_cast<const sockaddr*>(&server), sizeof(server)) == 0)
    return finishTcpConnect(sock, error);
  if (!wouldBlock()) {
    error = socketError("TCP connect");
    closeSocket(sock);
    sock = INVALID_SOCKET;
    return false;
  }

  if (waitForConnect(sock, stoken, error)) return finishTcpConnect(sock, error);

  closeSocket(sock);
  sock = INVALID_SOCKET;
  return false;
}

bool extractSignalRaw(const uint8_t data[8], uint8_t dlc, const Signal& sig, uint64_t& raw) {
  raw = 0;
  if (sig.startBit < 0 || sig.length <= 0 || sig.length > 64 || dlc > 8) return false;

  const int availableBits = static_cast<int>(dlc) * 8;
  if (sig.endianness == 1) {
    if (sig.startBit + sig.length > availableBits) return false;
    for (int i = 0; i < sig.length; i++) {
      const int bit = sig.startBit + i;
      raw |= static_cast<uint64_t>((data[bit / 8] >> (bit % 8)) & 0x1u) << i;
    }
    return true;
  }

  int bit = sig.startBit;
  for (int i = 0; i < sig.length; i++) {
    if (bit < 0 || bit >= availableBits) return false;
    raw = (raw << 1) | static_cast<uint64_t>((data[bit / 8] >> (bit % 8)) & 0x1u);
    bit = (bit % 8 == 0) ? bit + 15 : bit - 1;
  }
  return true;
}

int64_t signExtend(uint64_t raw, int bits) {
  if (bits <= 0 || bits >= 64) return static_cast<int64_t>(raw);

  const uint64_t signBit = uint64_t{1} << (bits - 1);
  const uint64_t mask = (uint64_t{1} << bits) - 1;
  raw &= mask;
  if ((raw & signBit) != 0) raw |= ~mask;
  return static_cast<int64_t>(raw);
}

bool decodeSignalValue(const canpPacket_t& packet, const Signal& sig, double& value) {
  uint64_t raw = 0;
  if (!extractSignalRaw(packet.data, packet.dlc, sig, raw)) return false;

  switch (sig.type) {
    case vFLOAT: {
      if (sig.length != 32) return false;
      const auto bits = static_cast<uint32_t>(raw);
      value = static_cast<double>(std::bit_cast<float>(bits)) * sig.scale + sig.offset;
      return true;
    }
    case vDOUBLE: {
      if (sig.length != 64) return false;
      value = std::bit_cast<double>(raw) * sig.scale + sig.offset;
      return true;
    }
    case vINT:
    default: {
      const double parsed = sig.isSigned ? static_cast<double>(signExtend(raw, sig.length))
                                         : static_cast<double>(raw);
      value = parsed * sig.scale + sig.offset;
      return true;
    }
  }
}

double batchTimeSeconds(uint64_t timestampMs) { return static_cast<double>(timestampMs) / 1000.0; }

void handleNetwork(const canpBatch_t& batch, Arena& arena) {
  double timeValue = batchTimeSeconds(batch.timestamp);
  const uint16_t count = batch.count > CANP_MAX_BATCH ? CANP_MAX_BATCH : batch.count;
  for (uint16_t i = 0; i < count; i++) {
    const canpPacket_t& packet = batch.packets[i];
    if (packet.dlc > 8) continue;

    const uint32_t id = canpGetId(&packet);
    if (id >= arena.messages.size()) continue;

    Message* msg = arena.messages[id];
    if (!msg || msg->signalCount == 0 || msg->signalCount > SIGNAL_MAX) continue;

    std::array<double, SIGNAL_MAX> values{};
    bool decoded = true;
    for (uint32_t signalIndex = 0; signalIndex < msg->signalCount; signalIndex++) {
      Signal* sig = msg->signals[signalIndex];
      if (!sig || !decodeSignalValue(packet, *sig, values[signalIndex])) {
        decoded = false;
        break;
      }
    }
    if (!decoded) continue;

    if (!arena.appendFrame(id, timeValue, values.data(), msg->signalCount)) {
      arena.clear(id);
      arena.appendFrame(id, timeValue, values.data(), msg->signalCount);
    }
  }
}

std::string timeNow() {
  auto now = std::chrono::system_clock::now();
  auto seconds = std::chrono::floor<std::chrono::seconds>(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds);
  return "[" + std::format("{:%H:%M:%S}.{:03}", seconds, ms.count()) + "]  ";
};

void Protocols::TCP(std::stop_token stoken, SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer,
                    TCPConfig config, Arena& arena) {
  SocketHandle sock = INVALID_SOCKET;
  std::string error{};
  if (!connectTcp(sock, config, stoken, error)) {
    if (!stoken.stop_requested()) publishError(txBuffer, error);
    return;
  }
  publishMessage(txBuffer, timeNow() + "TCP connected");

  canpBatch_t batch{};
  while (!stoken.stop_requested()) {
    std::string waitError{};
    const SocketWaitResult waitResult = waitForReadable(sock, stoken, waitError);
    if (waitResult == SocketWaitResult::Stopped) break;
    if (waitResult == SocketWaitResult::Error) {
      publishError(txBuffer, waitError);
      break;
    }

    int readStatus = canpReadBatch(sock, &batch);
    if (readStatus == CANP_READ_OK) {
      handleNetwork(batch, arena);
      continue;
    }
    if (readStatus == CANP_READ_CLOSED) {
      publishMessage(txBuffer, timeNow() + "TCP peer closed connection");
      break;
    }
    if (readStatus == CANP_READ_SOCKET_ERROR) {
      publishError(txBuffer, socketError("TCP recv"));
      break;
    }
    publishError(txBuffer, timeNow() + canpReadError(readStatus));
    break;
  }
  closeSocket(sock);
  publishMessage(txBuffer, timeNow() + "TCP stopped");
}

#ifdef LINUX

#include <signal.h>
#include <sys/wait.h>

#include <cstdlib>

namespace {

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

// candump -L prints one frame per line: "(<timestamp>) <iface> <ID>#<DATA>"
// e.g. "(1612345678.123456) can0 0C0#1122334455667788"
bool parseCandumpLine(const std::string& line, canpBatch_t& batch) {
  const size_t tsOpen = line.find('(');
  const size_t tsClose = line.find(')');
  if (tsOpen == std::string::npos || tsClose == std::string::npos || tsClose <= tsOpen) return false;

  const double ts = strtod(line.c_str() + tsOpen + 1, nullptr);

  const size_t hash = line.find('#', tsClose);
  if (hash == std::string::npos) return false;

  size_t idStart = line.rfind(' ', hash);
  idStart = (idStart == std::string::npos) ? tsClose + 1 : idStart + 1;
  if (idStart >= hash) return false;

  const uint32_t id = static_cast<uint32_t>(strtoul(line.substr(idStart, hash - idStart).c_str(), nullptr, 16));

  const char* dataStr = line.c_str() + hash + 1;
  if (*dataStr == 'R') return false;  // remote frame — no data

  uint8_t data[8]{};
  uint8_t dlc = 0;
  while (dlc < 8) {
    const int hi = hexNibble(dataStr[dlc * 2]);
    if (hi < 0) break;
    const int lo = hexNibble(dataStr[dlc * 2 + 1]);
    if (lo < 0) return false;
    data[dlc++] = static_cast<uint8_t>((hi << 4) | lo);
  }

  const uint16_t zeroDt[8]{};
  batch.timestamp = static_cast<uint64_t>(ts * 1000.0);
  batch.count = 1;
  batch.packets[0] = canpMakePacket(id, dlc, data, zeroDt);
  return true;
}

}  // namespace

void Protocols::Candump(std::stop_token stoken, SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer,
                        SPMCQueue<CANFrameEvent, 512>& frameEvents, PCANConfig config, Arena& arena) {
  int fds[2];
  if (pipe(fds) != 0) {
    publishError(txBuffer, timeNow() + "candump pipe creation failed");
    return;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    publishError(txBuffer, timeNow() + "candump fork failed");
    return;
  }
  if (pid == 0) {
    dup2(fds[1], STDOUT_FILENO);
    close(fds[0]);
    close(fds[1]);
    execlp("candump", "candump", "-L", config.channel, static_cast<char*>(nullptr));
    _exit(127);
  }
  close(fds[1]);
  const int fd = fds[0];

  publishMessage(txBuffer, timeNow() + "candump reading " + config.channel);

  canpBatch_t batch{};
  std::string pending{};
  char buf[512];
  while (!stoken.stop_requested()) {
    std::string waitError{};
    const SocketWaitResult waitResult = waitForReadable(fd, stoken, waitError);
    if (waitResult == SocketWaitResult::Stopped) break;
    if (waitResult == SocketWaitResult::Error) {
      publishError(txBuffer, waitError);
      break;
    }

    const ssize_t n = read(fd, buf, sizeof(buf));
    if (n == 0) {
      publishError(txBuffer, timeNow() + "candump exited — is " + config.channel + " up?");
      break;
    }
    if (n < 0) {
      if (errno == EINTR || errno == EAGAIN) continue;
      publishError(txBuffer, socketError("candump read"));
      break;
    }

    pending.append(buf, static_cast<size_t>(n));
    size_t newline;
    while ((newline = pending.find('\n')) != std::string::npos) {
      if (parseCandumpLine(pending.substr(0, newline), batch)) {
        handleNetwork(batch, arena);
        const canpPacket_t& packet = batch.packets[0];
        const uint32_t id = canpGetId(&packet);
        frameEvents.write([id, packet, timestampMs = batch.timestamp](CANFrameEvent& event) {
          event = {.id = id,
                   .dlc = packet.dlc,
                   .data = {packet.data[0], packet.data[1], packet.data[2], packet.data[3],
                            packet.data[4], packet.data[5], packet.data[6], packet.data[7]},
                   .timestampMs = timestampMs,
                   .transmitted = false};
        });
      }
      pending.erase(0, newline + 1);
    }
  }

  close(fd);
  kill(pid, SIGTERM);
  waitpid(pid, nullptr, 0);
  publishMessage(txBuffer, timeNow() + "candump stopped");
}

namespace {

uint64_t dashboardHton64(uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return (static_cast<uint64_t>(htonl(static_cast<uint32_t>(value))) << 32) |
         htonl(static_cast<uint32_t>(value >> 32));
#else
  return value;
#endif
}

bool relaySend(int socket, DashboardLink::MessageType type, uint32_t sequence, const void* payload,
               uint32_t bytes) {
  if (bytes > DashboardLink::kMaxPayload) return false;
  DashboardLink::Header header{.magic = htonl(DashboardLink::kMagic),
                               .version = htons(DashboardLink::kVersion),
                               .type = htons(static_cast<uint16_t>(type)),
                               .payloadBytes = htonl(bytes),
                               .sequence = htonl(sequence)};
  iovec parts[2]{{.iov_base = &header, .iov_len = sizeof(header)},
                 {.iov_base = const_cast<void*>(payload), .iov_len = bytes}};
  return canpWrite(socket, parts, bytes ? 2 : 1) > 0;
}

bool relayRead(int socket, DashboardLink::Header& header, std::vector<uint8_t>& payload) {
  if (canpRead(socket, &header, sizeof(header)) != CANP_READ_OK) return false;
  header.magic = ntohl(header.magic);
  header.version = ntohs(header.version);
  header.type = ntohs(header.type);
  header.payloadBytes = ntohl(header.payloadBytes);
  header.sequence = ntohl(header.sequence);
  if (header.magic != DashboardLink::kMagic || header.version != DashboardLink::kVersion ||
      header.payloadBytes > DashboardLink::kMaxPayload)
    return false;
  payload.resize(header.payloadBytes);
  return header.payloadBytes == 0 ||
         canpRead(socket, payload.data(), header.payloadBytes) == CANP_READ_OK;
}

std::vector<uint8_t> relayBatch(const CANFrameEvent& event, uint32_t sequence) {
  const uint16_t zeroDt[8]{};
  const canpPacket_t packet = canpMakePacket(event.id, event.dlc, event.data.data(), zeroDt);
  std::vector<uint8_t> payload(sizeof(DashboardLink::BatchHeader) + sizeof(packet));
  DashboardLink::BatchHeader header{.timestampMs = dashboardHton64(event.timestampMs),
                                    .sequence = htonl(sequence),
                                    .count = htons(1),
                                    .reserved = 0};
  std::memcpy(payload.data(), &header, sizeof(header));
  std::memcpy(payload.data() + sizeof(header), &packet, sizeof(packet));
  return payload;
}

bool writeCanFrames(const DashboardConfig& config, const std::vector<uint8_t>& payload,
                    std::string& error) {
  if (payload.size() < sizeof(DashboardLink::BatchHeader)) {
    error = "missing CAN write batch";
    return false;
  }
  DashboardLink::BatchHeader header{};
  std::memcpy(&header, payload.data(), sizeof(header));
  const uint16_t count = ntohs(header.count);
  if (count == 0 || count > CANP_MAX_BATCH ||
      payload.size() != sizeof(header) + static_cast<size_t>(count) * sizeof(canpPacket_t)) {
    error = "invalid CAN write batch";
    return false;
  }
  const int socket = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (socket < 0) {
    error = socketError("SocketCAN write socket");
    return false;
  }
  ifreq request{};
  std::snprintf(request.ifr_name, sizeof(request.ifr_name), "%s", config.channel);
  if (ioctl(socket, SIOCGIFINDEX, &request) < 0) {
    error = socketError("SocketCAN write interface");
    close(socket);
    return false;
  }
  sockaddr_can address{};
  address.can_family = AF_CAN;
  address.can_ifindex = request.ifr_ifindex;
  if (bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    error = socketError("SocketCAN write bind");
    close(socket);
    return false;
  }
  const auto* packets = reinterpret_cast<const canpPacket_t*>(payload.data() + sizeof(header));
  for (uint16_t i = 0; i < count; ++i) {
    const uint32_t id = canpGetId(&packets[i]);
    if (packets[i].dlc > 8 || id > CAN_SFF_MASK) {
      error = "remote write is not a classic standard CAN frame";
      close(socket);
      return false;
    }
    can_frame frame{};
    frame.can_id = id;
    frame.can_dlc = packets[i].dlc;
    std::copy_n(packets[i].data, frame.can_dlc, frame.data);
    if (::write(socket, &frame, sizeof(frame)) != sizeof(frame)) {
      error = socketError("SocketCAN write");
      close(socket);
      return false;
    }
  }
  close(socket);
  return true;
}

struct RelayClient {
  int socket = -1;
  std::mutex sendMutex{};
  std::atomic<bool> alive{true};
  std::jthread reader{};
  uint32_t nextSequence = 1;
};

}  // namespace

void Protocols::DashboardRelay(std::stop_token stoken,
                               SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer,
                               SPMCQueue<CANFrameEvent, 512>& frameEvents,
                               DashboardConfig config) {
  const int listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  const int discovery = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (listener < 0 || discovery < 0) {
    if (listener >= 0) close(listener);
    if (discovery >= 0) close(discovery);
    publishError(txBuffer, "Photon Dashboard relay: socket creation failed");
    return;
  }
  int reuse = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  setsockopt(discovery, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  // The dashboard is deliberately available only through the kart AP, even
  // when eth0 also has an address.
  constexpr char apInterface[] = "wlan0";
  setsockopt(listener, SOL_SOCKET, SO_BINDTODEVICE, apInterface, sizeof(apInterface));
  setsockopt(discovery, SOL_SOCKET, SO_BINDTODEVICE, apInterface, sizeof(apInterface));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(config.port);
  if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
      listen(listener, 4) < 0) {
    publishError(txBuffer, "Photon Dashboard relay: TCP bind failed");
    close(listener);
    close(discovery);
    return;
  }
  address.sin_port = htons(config.discoveryPort);
  if (bind(discovery, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    publishError(txBuffer, "Photon Dashboard relay: discovery bind failed");
    close(listener);
    close(discovery);
    return;
  }
  publishMessage(txBuffer, timeNow() + "Photon Dashboard Wi-Fi relay listening");

  std::mutex clientsMutex{};
  std::vector<std::shared_ptr<RelayClient>> clients{};
  std::shared_ptr<RelayClient> controller{};
  auto controllerHeartbeat = std::chrono::steady_clock::time_point{};
  auto sendStatus = [&](const std::shared_ptr<RelayClient>& client, bool granted) {
    const uint8_t flags = (config.remoteWritesEnabled ? DashboardLink::RemoteWritesEnabled : 0) |
                          (granted ? DashboardLink::ControllerLeaseHeld : 0);
    std::lock_guard sendLock(client->sendMutex);
    return relaySend(client->socket, DashboardLink::MessageType::Status, client->nextSequence++,
                     &flags, sizeof(flags));
  };
  auto frameReader = frameEvents.getReader();
  uint32_t telemetrySequence = 1;
  while (!stoken.stop_requested()) {
    while (CANFrameEvent* event = frameReader.read()) {
      if (event->transmitted) continue;
      const auto payload = relayBatch(*event, telemetrySequence++);
      std::lock_guard lock(clientsMutex);
      for (const auto& client : clients) {
        if (!client->alive.load()) continue;
        std::lock_guard sendLock(client->sendMutex);
        if (!relaySend(client->socket, DashboardLink::MessageType::Telemetry, telemetrySequence++,
                       payload.data(), static_cast<uint32_t>(payload.size())))
          client->alive.store(false);
      }
    }
    {
      std::lock_guard lock(clientsMutex);
      if (controller && (!controller->alive.load() ||
                         std::chrono::steady_clock::now() - controllerHeartbeat >
                             std::chrono::seconds(3)))
        controller.reset();
      clients.erase(std::remove_if(clients.begin(), clients.end(),
                                   [](const auto& client) { return !client->alive.load(); }),
                    clients.end());
    }

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(listener, &readSet);
    FD_SET(discovery, &readSet);
    const int maxFd = std::max(listener, discovery) + 1;
    timeval timeout{};
    timeout.tv_usec = 10000;
    if (select(maxFd, &readSet, nullptr, nullptr, &timeout) <= 0) continue;
    if (FD_ISSET(discovery, &readSet)) {
      DashboardLink::DiscoveryRequest request{};
      sockaddr_in source{};
      socklen_t sourceBytes = sizeof(source);
      const ssize_t bytes = recvfrom(discovery, &request, sizeof(request), 0,
                                     reinterpret_cast<sockaddr*>(&source), &sourceBytes);
      if (bytes == sizeof(request) && ntohl(request.magic) == DashboardLink::kMagic &&
          ntohs(request.version) == DashboardLink::kVersion) {
        DashboardLink::DiscoveryResponse response{};
        response.magic = htonl(DashboardLink::kMagic);
        response.version = htons(DashboardLink::kVersion);
        response.streamPort = htons(config.port);
        response.statusFlags = config.remoteWritesEnabled ? DashboardLink::RemoteWritesEnabled : 0;
        std::snprintf(response.name.data(), response.name.size(), "Photon CM5 Dashboard");
        sendto(discovery, &response, sizeof(response), 0, reinterpret_cast<sockaddr*>(&source),
               sourceBytes);
      }
    }
    if (!FD_ISSET(listener, &readSet)) continue;
    sockaddr_in source{};
    socklen_t sourceBytes = sizeof(source);
    const int socket = accept(listener, reinterpret_cast<sockaddr*>(&source), &sourceBytes);
    if (socket < 0) continue;
    auto client = std::make_shared<RelayClient>();
    client->socket = socket;
    {
      std::lock_guard lock(clientsMutex);
      if (clients.size() >= 4) {
        close(socket);
        continue;
      }
      clients.push_back(client);
    }
    sendStatus(client, false);
    RelayClient* clientPtr = client.get();
    client->reader = std::jthread([&, clientPtr](std::stop_token clientStop) {
      const auto sendClientStatus = [&](bool granted) {
        const uint8_t flags =
            (config.remoteWritesEnabled ? DashboardLink::RemoteWritesEnabled : 0) |
            (granted ? DashboardLink::ControllerLeaseHeld : 0);
        std::lock_guard sendLock(clientPtr->sendMutex);
        return relaySend(clientPtr->socket, DashboardLink::MessageType::Status,
                         clientPtr->nextSequence++, &flags, sizeof(flags));
      };
      while (!clientStop.stop_requested() && clientPtr->alive.load()) {
        DashboardLink::Header header{};
        std::vector<uint8_t> payload{};
        if (!relayRead(clientPtr->socket, header, payload)) break;
        const auto type = static_cast<DashboardLink::MessageType>(header.type);
        bool granted = false;
        if (type == DashboardLink::MessageType::ArmRequest) {
          std::lock_guard lock(clientsMutex);
          if (config.remoteWritesEnabled && (!controller || controller.get() == clientPtr)) {
            const auto current = std::find_if(clients.begin(), clients.end(),
                                              [clientPtr](const auto& candidate) {
                                                return candidate.get() == clientPtr;
                                              });
            if (current == clients.end()) break;
            controller = *current;
            controllerHeartbeat = std::chrono::steady_clock::now();
            granted = true;
          }
          std::lock_guard sendLock(clientPtr->sendMutex);
          relaySend(clientPtr->socket, granted ? DashboardLink::MessageType::ArmGranted
                                             : DashboardLink::MessageType::ArmDenied,
                    clientPtr->nextSequence++, nullptr, 0);
        } else if (type == DashboardLink::MessageType::Heartbeat) {
          std::lock_guard lock(clientsMutex);
          if (controller && controller.get() == clientPtr)
            controllerHeartbeat = std::chrono::steady_clock::now();
        } else if (type == DashboardLink::MessageType::ArmRelease) {
          std::lock_guard lock(clientsMutex);
          if (controller && controller.get() == clientPtr) controller.reset();
          sendClientStatus(false);
        } else if (type == DashboardLink::MessageType::CanWrite) {
          {
            std::lock_guard lock(clientsMutex);
            granted = config.remoteWritesEnabled && controller && controller.get() == clientPtr &&
                      std::chrono::steady_clock::now() - controllerHeartbeat <=
                          std::chrono::seconds(3);
          }
          std::string writeError{};
          if (!granted || !writeCanFrames(config, payload, writeError)) {
            if (!granted) writeError = "remote write rejected: controller lease required";
            publishError(txBuffer, "Photon Dashboard: " + writeError);
            sendClientStatus(false);
          }
        }
      }
      shutdown(clientPtr->socket, SHUT_RDWR);
      close(clientPtr->socket);
      std::lock_guard lock(clientsMutex);
      if (controller && controller.get() == clientPtr) controller.reset();
      clientPtr->alive.store(false);
    });
  }
  {
    std::lock_guard lock(clientsMutex);
    for (const auto& client : clients) {
      client->alive.store(false);
      shutdown(client->socket, SHUT_RDWR);
      client->reader.request_stop();
    }
  }
  close(listener);
  close(discovery);
  publishMessage(txBuffer, timeNow() + "Photon Dashboard relay stopped");
}

#endif
