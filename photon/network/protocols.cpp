#include "protocols.hpp"


#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <thread>

#include "../parse/arena.hpp"
#include "canp.h"
#include "dashboardLink.hpp"

#ifdef _WIN32
#include <Ws2tcpip.h>
#include <windows.h>
#include <winsock2.h>
using SocketHandle = SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

#if defined(LINUX)
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#endif

void publishMessage(SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer, std::string message) {
  txBuffer.write([&](ProtocolReceiveVariant& out) { out = ProtocolMessage{.message = message}; });
}

void publishError(SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer, std::string error) {
  txBuffer.write([&](ProtocolReceiveVariant& out) { out = ProtocolError{.error = error}; });
}

void sendTimelineRequests(std::stop_token stoken, SocketHandle sock,
                          TimelineCursorMailbox& timelineCursor, uint64_t observed) {
  while (!stoken.stop_requested()) {
    timelineCursor.sequence.wait(observed, std::memory_order_acquire);
    if (stoken.stop_requested()) break;
    observed = timelineCursor.sequence.load(std::memory_order_acquire);
    const uint64_t request = timelineCursor.request.load(std::memory_order_relaxed);
    if (canpWriteTimelineCommand(sock, TimelineCursorMailbox::command(request),
                                 TimelineCursorMailbox::timestamp(request)) != 1)
      break;
  }
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

bool sendRecordStartCommand(SocketHandle sock, bool recordOnConnect, std::string& error) {
  if (!recordOnConnect) return true;

  constexpr std::string_view recordCommand{"dLV8YO8%M#6v\n"};
  const char* data = recordCommand.data();
  size_t remaining = recordCommand.size();
  while (remaining > 0) {
    const int chunk = static_cast<int>(std::min<size_t>(remaining, 0x7fffffff));
    const auto bytesSent = send(sock, data, chunk, 0);
    if (bytesSent <= 0) {
      error = socketError("record command");
      return false;
    }
    data += bytesSent;
    remaining -= static_cast<size_t>(bytesSent);
  }
  return true;
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
#ifdef SO_NOSIGPIPE
  int noSigPipe = 1;
  setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&noSigPipe),
             sizeof(noSigPipe));
#endif

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

  if (connect(sock, reinterpret_cast<const sockaddr*>(&server), sizeof(server)) == 0) {
    if (finishTcpConnect(sock, error) && sendRecordStartCommand(sock, config.recordOnConnect, error))
      return true;
    closeSocket(sock);
    sock = INVALID_SOCKET;
    return false;
  }
  if (!wouldBlock()) {
    error = socketError("TCP connect");
    closeSocket(sock);
    sock = INVALID_SOCKET;
    return false;
  }

  if (waitForConnect(sock, stoken, error)) {
    if (finishTcpConnect(sock, error) && sendRecordStartCommand(sock, config.recordOnConnect, error))
      return true;
    closeSocket(sock);
    sock = INVALID_SOCKET;
    return false;
  }

  closeSocket(sock);
  sock = INVALID_SOCKET;
  return false;
}

uint64_t hostToNetwork64(uint64_t value) {
#if defined(_WIN32) || __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return (static_cast<uint64_t>(htonl(static_cast<uint32_t>(value))) << 32) |
         htonl(static_cast<uint32_t>(value >> 32));
#else
  return value;
#endif
}

uint64_t networkToHost64(uint64_t value) {
#if defined(_WIN32) || __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return (static_cast<uint64_t>(ntohl(static_cast<uint32_t>(value))) << 32) |
         ntohl(static_cast<uint32_t>(value >> 32));
#else
  return value;
#endif
}

bool writeDashboardMessage(SocketHandle socket, DashboardLink::MessageType type, uint32_t sequence,
                           const void* payload, uint32_t payloadBytes) {
  if (payloadBytes > DashboardLink::kMaxPayload) return false;
  DashboardLink::Header header{.magic = htonl(DashboardLink::kMagic),
                               .version = htons(DashboardLink::kVersion),
                               .type = htons(static_cast<uint16_t>(type)),
                               .payloadBytes = htonl(payloadBytes),
                               .sequence = htonl(sequence)};
  iovec parts[2]{{.iov_base = &header, .iov_len = sizeof(header)},
                 {.iov_base = const_cast<void*>(payload), .iov_len = payloadBytes}};
  return canpWrite(socket, parts, payloadBytes ? 2 : 1) > 0;
}

bool readDashboardMessage(SocketHandle socket, DashboardLink::Header& header,
                          std::vector<uint8_t>& payload) {
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

bool decodeDashboardBatch(const std::vector<uint8_t>& payload, canpBatch_t& batch) {
  if (payload.size() < sizeof(DashboardLink::BatchHeader)) return false;
  DashboardLink::BatchHeader header{};
  std::memcpy(&header, payload.data(), sizeof(header));
  const uint16_t count = ntohs(header.count);
  const size_t expected = sizeof(header) + static_cast<size_t>(count) * sizeof(canpPacket_t);
  if (count == 0 || count > CANP_MAX_BATCH || payload.size() != expected) return false;
  batch.timestamp = networkToHost64(header.timestampMs);
  batch.seq = ntohl(header.sequence);
  batch.count = count;
  std::memcpy(batch.packets, payload.data() + sizeof(header),
              static_cast<size_t>(count) * sizeof(canpPacket_t));
  return true;
}

std::vector<uint8_t> encodeDashboardBatch(const canpPacket_t* packets, uint16_t count,
                                          uint64_t timestampMs, uint32_t sequence) {
  std::vector<uint8_t> payload(sizeof(DashboardLink::BatchHeader) +
                               static_cast<size_t>(count) * sizeof(canpPacket_t));
  DashboardLink::BatchHeader header{.timestampMs = hostToNetwork64(timestampMs),
                                    .sequence = htonl(sequence),
                                    .count = htons(count),
                                    .reserved = 0};
  std::memcpy(payload.data(), &header, sizeof(header));
  std::memcpy(payload.data() + sizeof(header), packets,
              static_cast<size_t>(count) * sizeof(canpPacket_t));
  return payload;
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
  return CANCodec::decodeSignal(packet.data, packet.dlc, sig, value);
}

bool insertSignalRaw(uint8_t data[8], uint8_t dlc, const Signal& sig, uint64_t raw) {
  if (sig.startBit < 0 || sig.length <= 0 || sig.length > 64 || dlc > 8) return false;
  const int availableBits = static_cast<int>(dlc) * 8;
  if (sig.endianness == 1) {
    if (sig.startBit + sig.length > availableBits) return false;
    for (int i = 0; i < sig.length; ++i) {
      const int bit = sig.startBit + i;
      const uint8_t mask = static_cast<uint8_t>(1u << (bit % 8));
      if (((raw >> i) & 1u) != 0)
        data[bit / 8] |= mask;
      else
        data[bit / 8] &= static_cast<uint8_t>(~mask);
    }
    return true;
  }

  int bit = sig.startBit;
  for (int i = 0; i < sig.length; ++i) {
    if (bit < 0 || bit >= availableBits) return false;
    const int rawBit = sig.length - 1 - i;
    const uint8_t mask = static_cast<uint8_t>(1u << (bit % 8));
    if (((raw >> rawBit) & 1u) != 0)
      data[bit / 8] |= mask;
    else
      data[bit / 8] &= static_cast<uint8_t>(~mask);
    bit = (bit % 8 == 0) ? bit + 15 : bit - 1;
  }
  return true;
}

bool physicalToRaw(const Signal& sig, double physicalValue, uint64_t& raw, std::string& error) {
  if (!std::isfinite(physicalValue) || !std::isfinite(sig.scale) || sig.scale == 0.0 ||
      !std::isfinite(sig.offset)) {
    error = "signal value, scale, and offset must be finite; scale cannot be zero";
    return false;
  }
  if (sig.max > sig.min && (physicalValue < sig.min || physicalValue > sig.max)) {
    error = std::format("value {} is outside DBC range [{}, {}]", physicalValue, sig.min, sig.max);
    return false;
  }

  const double encoded = (physicalValue - sig.offset) / sig.scale;
  if (sig.type == vFLOAT) {
    if (sig.length != 32 || !std::isfinite(encoded)) {
      error = "DBC float signal must be a finite 32-bit value";
      return false;
    }
    raw = std::bit_cast<uint32_t>(static_cast<float>(encoded));
    return true;
  }
  if (sig.type == vDOUBLE) {
    if (sig.length != 64 || !std::isfinite(encoded)) {
      error = "DBC double signal must be a finite 64-bit value";
      return false;
    }
    raw = std::bit_cast<uint64_t>(encoded);
    return true;
  }

  const long double rounded = std::round(static_cast<long double>(encoded));
  if (sig.isSigned) {
    const long double minimum = sig.length == 64
                                    ? static_cast<long double>(std::numeric_limits<int64_t>::min())
                                    : -std::ldexp(1.0L, sig.length - 1);
    const long double maximum = sig.length == 64
                                    ? static_cast<long double>(std::numeric_limits<int64_t>::max())
                                    : std::ldexp(1.0L, sig.length - 1) - 1.0L;
    if (rounded < minimum || rounded > maximum) {
      error = "value cannot be represented by the signed DBC bit width";
      return false;
    }
    raw = static_cast<uint64_t>(static_cast<int64_t>(rounded));
  } else {
    const long double maximum = sig.length == 64
                                    ? static_cast<long double>(std::numeric_limits<uint64_t>::max())
                                    : std::ldexp(1.0L, sig.length) - 1.0L;
    if (rounded < 0.0L || rounded > maximum) {
      error = "value cannot be represented by the unsigned DBC bit width";
      return false;
    }
    raw = static_cast<uint64_t>(rounded);
  }
  return true;
}

namespace CANCodec {
bool decodeSignal(const uint8_t data[8], uint8_t dlc, const Signal& signal, double& value) {
  uint64_t raw = 0;
  if (!extractSignalRaw(data, dlc, signal, raw)) return false;
  switch (signal.type) {
    case vFLOAT:
      if (signal.length != 32) return false;
      value = static_cast<double>(std::bit_cast<float>(static_cast<uint32_t>(raw))) * signal.scale +
              signal.offset;
      return true;
    case vDOUBLE:
      if (signal.length != 64) return false;
      value = std::bit_cast<double>(raw) * signal.scale + signal.offset;
      return true;
    case vINT:
    default: {
      const double parsed = signal.isSigned ? static_cast<double>(signExtend(raw, signal.length))
                                            : static_cast<double>(raw);
      value = parsed * signal.scale + signal.offset;
      return true;
    }
  }
}

bool encodeSignal(uint8_t data[8], uint8_t dlc, const Signal& signal, double physicalValue,
                  std::string& error) {
  uint64_t raw = 0;
  if (!physicalToRaw(signal, physicalValue, raw, error)) return false;
  if (!insertSignalRaw(data, dlc, signal, raw)) {
    error = "DBC signal bit range does not fit in the CAN payload";
    return false;
  }
  return true;
}
}  // namespace CANCodec

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
                    TCPConfig config, Arena& arena, TimelineCursorMailbox& timelineCursor) {
  SocketHandle sock = INVALID_SOCKET;
  std::string error{};
  if (!connectTcp(sock, config, stoken, error)) {
    if (!stoken.stop_requested()) publishError(txBuffer, error);
    return;
  }
  publishMessage(txBuffer, timeNow() + "TCP connected");
  const uint64_t timelineSequence = timelineCursor.sequence.load(std::memory_order_acquire);
  std::jthread timelineSender(
      [sock, &timelineCursor, timelineSequence](std::stop_token senderToken) {
        sendTimelineRequests(senderToken, sock, timelineCursor, timelineSequence);
      });

  canpBatch_t batch{};
  canpTimelineStatus_t timelineStatus{};
  while (!stoken.stop_requested()) {
    std::string waitError{};
    const SocketWaitResult waitResult = waitForReadable(sock, stoken, waitError);
    if (waitResult == SocketWaitResult::Stopped) break;
    if (waitResult == SocketWaitResult::Error) {
      publishError(txBuffer, waitError);
      break;
    }

    int readStatus = canpReadStream(sock, &batch, &timelineStatus);
    if (readStatus == CANP_READ_TIMELINE_STATUS) {
      if (timelineStatus.mode == CANP_TIMELINE_PLAY || timelineStatus.mode == CANP_TIMELINE_LIVE) {
        arena.clearAll();
        timelineCursor.latestTimestampMs.store(0, std::memory_order_relaxed);
      }
      timelineCursor.response.store(
          TimelineCursorMailbox::pack(timelineStatus.mode, timelineStatus.timestamp),
          std::memory_order_relaxed);
      timelineCursor.statusSequence.fetch_add(1, std::memory_order_release);
      timelineCursor.statusSequence.notify_all();
      continue;
    }
    if (readStatus == CANP_READ_OK) {
      handleNetwork(batch, arena);
      timelineCursor.latestTimestampMs.store(batch.timestamp, std::memory_order_release);
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
  timelineSender.request_stop();
  timelineCursor.sequence.fetch_add(1, std::memory_order_release);
  timelineCursor.sequence.notify_all();
  timelineSender.join();
  closeSocket(sock);
  publishMessage(txBuffer, timeNow() + "TCP stopped");
}

namespace {
struct CachedCANFrame {
  bool valid = false;
  uint8_t dlc = 0;
  std::array<uint8_t, 8> data{};
};

using CANFrameCache = std::array<CachedCANFrame, MESSAGE_MAX>;

void receiveCANFrame(uint32_t id, uint8_t dlc, const uint8_t data[8], uint64_t timestampMs,
                     CANFrameCache& cache, Arena& arena,
                     SPMCQueue<CANFrameEvent, 512>& frameEvents, bool transmitted) {
  if (id >= MESSAGE_MAX || dlc > 8) return;
  CachedCANFrame& cached = cache[id];
  cached.valid = true;
  cached.dlc = dlc;
  std::copy_n(data, dlc, cached.data.begin());

  const uint16_t deltaTimes[8]{};
  canpBatch_t batch{};
  batch.timestamp = timestampMs;
  batch.count = 1;
  batch.packets[0] = canpMakePacket(id, dlc, data, deltaTimes);
  handleNetwork(batch, arena);
  frameEvents.write([id, dlc, data = cached.data, timestampMs, transmitted](CANFrameEvent& event) {
    event = {.id = id,
             .dlc = dlc,
             .data = data,
             .timestampMs = timestampMs,
             .transmitted = transmitted};
  });
}

bool prepareCANWrite(const CANFrameWrite& request, Arena& arena, CANFrameCache& cache,
                     uint32_t& messageId, uint8_t& dlc, std::array<uint8_t, 8>& data,
                     std::string& error) {
  Message* message = nullptr;
  for (uint32_t candidateId : arena.validIds) {
    Message* candidate =
        candidateId < arena.messages.size() ? arena.messages[candidateId] : nullptr;
    if (candidate && candidate->name == request.messageName) {
      message = candidate;
      messageId = candidateId;
      break;
    }
  }
  if (!message) {
    error = std::format("DBC message '{}' no longer exists", request.messageName);
    return false;
  }

  if (message->dlc > 8) {
    error = "classic PCAN transport only supports payloads up to 8 bytes";
    return false;
  }

  dlc = static_cast<uint8_t>(message->dlc);
  const CachedCANFrame& cached = cache[messageId];
  if (cached.valid && cached.dlc == dlc) {
    data = cached.data;
  } else if (message->signalCount > 1 && !request.allowUnseenFrame) {
    error = "wait for this multi-signal message to be received before editing it";
    return false;
  }
  for (const CANSignalValue& value : request.values) {
    Signal* signal = nullptr;
    for (uint32_t signalIndex = 0; signalIndex < message->signalCount; ++signalIndex) {
      if (message->signals[signalIndex] && message->signals[signalIndex]->name == value.signalName) {
        signal = message->signals[signalIndex];
        break;
      }
    }
    if (!signal) {
      error = std::format("DBC signal '{}.{}' no longer exists", request.messageName,
                          value.signalName);
      return false;
    }
    if (!CANCodec::encodeSignal(data.data(), dlc, *signal, value.physicalValue, error))
      return false;
  }
  return true;
}

uint64_t steadyTimestampMs() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

#ifdef _WIN32
using TPCANHandle = uint16_t;
using TPCANStatus = uint32_t;
using TPCANBaudrate = uint16_t;

struct TPCANMsg {
  uint32_t ID;
  uint8_t MSGTYPE;
  uint8_t LEN;
  uint8_t DATA[8];
};

struct TPCANTimestamp {
  uint32_t millis;
  uint16_t millis_overflow;
  uint16_t micros;
};

constexpr TPCANStatus PCAN_ERROR_OK = 0x00000;
constexpr TPCANStatus PCAN_ERROR_QRCVEMPTY = 0x00020;
constexpr uint8_t PCAN_MESSAGE_STANDARD = 0x00;
constexpr uint8_t PCAN_MESSAGE_RTR = 0x01;
constexpr uint8_t PCAN_MESSAGE_EXTENDED = 0x02;
constexpr uint8_t PCAN_MESSAGE_STATUS = 0x80;
constexpr TPCANHandle PCAN_USBBUS1 = 0x51;

using CANInitializeFn = TPCANStatus(WINAPI*)(TPCANHandle, TPCANBaudrate, uint8_t, uint32_t,
                                             uint16_t);
using CANUninitializeFn = TPCANStatus(WINAPI*)(TPCANHandle);
using CANReadFn = TPCANStatus(WINAPI*)(TPCANHandle, TPCANMsg*, TPCANTimestamp*);
using CANWriteFn = TPCANStatus(WINAPI*)(TPCANHandle, TPCANMsg*);
using CANGetErrorTextFn = TPCANStatus(WINAPI*)(TPCANStatus, uint16_t, char*);
using CANSetValueFn = TPCANStatus(WINAPI*)(TPCANHandle, uint8_t, void*, uint32_t);

TPCANHandle pcanChannel(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  constexpr std::string_view prefixes[] = {"PCAN_USBBUS", "USBBUS", "USB"};
  for (std::string_view prefix : prefixes) {
    if (name.starts_with(prefix)) {
      try {
        const int index = std::stoi(name.substr(prefix.size()));
        if (index >= 1 && index <= 16) return static_cast<TPCANHandle>(PCAN_USBBUS1 + index - 1);
      } catch (...) {
      }
    }
  }
  try {
    return static_cast<TPCANHandle>(std::stoul(name, nullptr, 0));
  } catch (...) {
    return 0;
  }
}

TPCANBaudrate pcanBitrate(uint32_t kbps) {
  switch (kbps) {
    case 1000:
      return 0x0014;
    case 800:
      return 0x0016;
    case 500:
      return 0x001C;
    case 250:
      return 0x011C;
    case 125:
      return 0x031C;
    case 100:
      return 0x432F;
    case 50:
      return 0x472F;
    case 20:
      return 0x532F;
    case 10:
      return 0x672F;
    case 5:
      return 0x7F7F;
    default:
      return 0;
  }
}

std::string pcanStatusText(TPCANStatus status, CANGetErrorTextFn getErrorText) {
  char text[256]{};
  if (getErrorText && getErrorText(status, 0, text) == PCAN_ERROR_OK && text[0] != '\0')
    return text;
  return std::format("PCAN status 0x{:X}", status);
}
#endif
}  // namespace

void Protocols::PCAN(std::stop_token stoken, SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer,
                     SPMCQueue<CANFrameWrite, 64>& writeBuffer,
                     SPMCQueue<CANFrameEvent, 512>& frameEvents, PCANConfig config, Arena& arena) {
  CANFrameCache cache{};
  auto writeReader = writeBuffer.getReader();

#ifdef _WIN32
  HMODULE library = LoadLibraryA("PCANBasic.dll");
  if (!library) {
    publishError(txBuffer,
                 "PCANBasic.dll was not found. Install the PEAK PCAN-Basic Windows package.");
    return;
  }
  const auto initialize =
      reinterpret_cast<CANInitializeFn>(GetProcAddress(library, "CAN_Initialize"));
  const auto uninitialize =
      reinterpret_cast<CANUninitializeFn>(GetProcAddress(library, "CAN_Uninitialize"));
  const auto read = reinterpret_cast<CANReadFn>(GetProcAddress(library, "CAN_Read"));
  const auto write = reinterpret_cast<CANWriteFn>(GetProcAddress(library, "CAN_Write"));
  const auto getErrorText =
      reinterpret_cast<CANGetErrorTextFn>(GetProcAddress(library, "CAN_GetErrorText"));
  const auto setValue = reinterpret_cast<CANSetValueFn>(GetProcAddress(library, "CAN_SetValue"));
  if (!initialize || !uninitialize || !read || !write) {
    publishError(txBuffer, "PCANBasic.dll does not expose the required classic CAN API");
    FreeLibrary(library);
    return;
  }

  const TPCANHandle channel = pcanChannel(config.channel);
  const TPCANBaudrate bitrate = config.useBtr
                                    ? static_cast<TPCANBaudrate>((config.btr0 << 8) | config.btr1)
                                    : pcanBitrate(config.bitrateKbps);
  if (channel == 0 || bitrate == 0) {
    publishError(txBuffer, "Invalid PCAN channel or unsupported classic CAN bitrate");
    FreeLibrary(library);
    return;
  }
  constexpr uint8_t PCAN_BUSOFF_AUTORESET = 0x07;
  constexpr uint8_t PCAN_LISTEN_ONLY = 0x08;
  uint32_t enabled = 1;
  if (setValue && config.listenOnly) setValue(channel, PCAN_LISTEN_ONLY, &enabled, sizeof(enabled));
  const TPCANStatus initStatus = initialize(channel, bitrate, 0, 0, 0);
  if (initStatus != PCAN_ERROR_OK) {
    publishError(txBuffer,
                 "PCAN initialization failed: " + pcanStatusText(initStatus, getErrorText));
    FreeLibrary(library);
    return;
  }
  if (setValue && config.busoffReset)
    setValue(channel, PCAN_BUSOFF_AUTORESET, &enabled, sizeof(enabled));
  publishMessage(txBuffer, timeNow() + std::format("PCAN connected ({}, {} kbit/s)", config.channel,
                                                   config.bitrateKbps));

  while (!stoken.stop_requested()) {
    while (CANFrameWrite* request = writeReader.read()) {
      if (config.listenOnly) continue;
      uint32_t messageId = 0;
      uint8_t dlc = 0;
      std::array<uint8_t, 8> data{};
      std::string error{};
      if (!prepareCANWrite(*request, arena, cache, messageId, dlc, data, error)) {
        publishError(txBuffer, "CAN send rejected: " + error);
        continue;
      }
      TPCANMsg message{};
      message.ID = messageId;
      message.MSGTYPE = PCAN_MESSAGE_STANDARD;
      message.LEN = dlc;
      std::copy_n(data.begin(), dlc, message.DATA);
      const TPCANStatus status = write(channel, &message);
      if (status != PCAN_ERROR_OK) {
        publishError(txBuffer, "PCAN write failed: " + pcanStatusText(status, getErrorText));
      } else {
        receiveCANFrame(messageId, dlc, data.data(), steadyTimestampMs(), cache, arena, frameEvents,
                        true);
      }
    }

    bool received = false;
    while (!stoken.stop_requested()) {
      TPCANMsg message{};
      TPCANTimestamp timestamp{};
      const TPCANStatus status = read(channel, &message, &timestamp);
      if (status == PCAN_ERROR_QRCVEMPTY) break;
      if (status != PCAN_ERROR_OK) {
        publishError(txBuffer, "PCAN read failed: " + pcanStatusText(status, getErrorText));
        break;
      }
      received = true;
      if ((message.MSGTYPE & PCAN_MESSAGE_RTR) != 0 ||
          (message.MSGTYPE & PCAN_MESSAGE_EXTENDED) != 0 ||
          (message.MSGTYPE & PCAN_MESSAGE_STATUS) != 0)
        continue;
      const uint64_t timestampMs = static_cast<uint64_t>(timestamp.millis) +
                                   (static_cast<uint64_t>(timestamp.millis_overflow) << 32);
      receiveCANFrame(message.ID, message.LEN, message.DATA, timestampMs, cache, arena, frameEvents,
                      false);
    }
    if (!received) std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  uninitialize(channel);
  FreeLibrary(library);
#elif defined(LINUX)
  const int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (sock < 0) {
    publishError(txBuffer, socketError("SocketCAN socket creation"));
    return;
  }
  ifreq interfaceRequest{};
  std::snprintf(interfaceRequest.ifr_name, sizeof(interfaceRequest.ifr_name), "%s", config.channel);
  if (ioctl(sock, SIOCGIFINDEX, &interfaceRequest) < 0) {
    publishError(txBuffer, socketError("SocketCAN interface lookup"));
    close(sock);
    return;
  }
  sockaddr_can address{};
  address.can_family = AF_CAN;
  address.can_ifindex = interfaceRequest.ifr_ifindex;
  if (bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
      !setNonBlocking(sock)) {
    publishError(txBuffer, socketError("SocketCAN bind"));
    close(sock);
    return;
  }
  publishMessage(txBuffer, timeNow() + "PCAN/SocketCAN connected on " + config.channel);
  while (!stoken.stop_requested()) {
    while (CANFrameWrite* request = writeReader.read()) {
      if (config.listenOnly) continue;
      uint32_t messageId = 0;
      uint8_t dlc = 0;
      std::array<uint8_t, 8> data{};
      std::string error{};
      if (!prepareCANWrite(*request, arena, cache, messageId, dlc, data, error)) {
        publishError(txBuffer, "CAN send rejected: " + error);
        continue;
      }
      can_frame frame{};
      frame.can_id = messageId;
      frame.can_dlc = dlc;
      std::copy_n(data.begin(), dlc, frame.data);
      if (::write(sock, &frame, sizeof(frame)) != sizeof(frame))
        publishError(txBuffer, socketError("SocketCAN write"));
      else
        receiveCANFrame(messageId, dlc, data.data(), steadyTimestampMs(), cache, arena, frameEvents,
                        true);
    }
    can_frame frame{};
    const ssize_t bytes = ::read(sock, &frame, sizeof(frame));
    if (bytes == sizeof(frame) &&
        (frame.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) == 0)
      receiveCANFrame(frame.can_id & CAN_SFF_MASK, frame.can_dlc, frame.data, steadyTimestampMs(),
                      cache, arena, frameEvents, false);
    else
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  close(sock);
#else
  (void)stoken;
  (void)writeReader;
  (void)frameEvents;
  (void)config;
  (void)arena;
  publishError(txBuffer, "PCAN is not supported on this platform");
#endif
  publishMessage(txBuffer, timeNow() + "PCAN stopped");
}

void Protocols::Dashboard(std::stop_token stoken,
                          SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer,
                          SPMCQueue<CANFrameWrite, 64>& writeBuffer,
                          SPMCQueue<CANFrameEvent, 512>& frameEvents,
                          DashboardConfig config, Arena& arena,
                          std::atomic<bool>& armRequested,
                          std::atomic<bool>& transmitAvailable,
                          std::atomic<bool>& controlsArmed) {
  TCPConfig endpoint{};
  endpoint.port = config.port;
  std::snprintf(endpoint.ip, sizeof(endpoint.ip), "%s", config.ip);
  SocketHandle socket = INVALID_SOCKET;
  std::string error{};
  if (!connectTcp(socket, endpoint, stoken, error)) {
    if (!stoken.stop_requested()) publishError(txBuffer, "Photon Dashboard: " + error);
    return;
  }

  uint32_t sequence = 1;
  if (!writeDashboardMessage(socket, DashboardLink::MessageType::Hello, sequence++, nullptr, 0)) {
    closeSocket(socket);
    publishError(txBuffer, "Photon Dashboard: hello failed");
    return;
  }
  publishMessage(txBuffer, timeNow() + "Photon Dashboard connected");

  CANFrameCache cache{};
  auto writeReader = writeBuffer.getReader();
  bool lastRequested = false;
  auto nextHeartbeat = std::chrono::steady_clock::now();
  while (!stoken.stop_requested()) {
    const bool requested = armRequested.load(std::memory_order_acquire);
    if (requested != lastRequested) {
      const auto type = requested ? DashboardLink::MessageType::ArmRequest
                                  : DashboardLink::MessageType::ArmRelease;
      if (!writeDashboardMessage(socket, type, sequence++, nullptr, 0)) break;
      lastRequested = requested;
      if (!requested) controlsArmed.store(false, std::memory_order_release);
    }
    if (requested && std::chrono::steady_clock::now() >= nextHeartbeat) {
      if (!writeDashboardMessage(socket, DashboardLink::MessageType::Heartbeat, sequence++, nullptr, 0))
        break;
      nextHeartbeat = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    }

    while (CANFrameWrite* request = writeReader.read()) {
      if (!controlsArmed.load(std::memory_order_acquire)) continue;
      uint32_t messageId = 0;
      uint8_t dlc = 0;
      std::array<uint8_t, 8> data{};
      std::string writeError{};
      if (!prepareCANWrite(*request, arena, cache, messageId, dlc, data, writeError)) {
        publishError(txBuffer, "Dashboard CAN send rejected: " + writeError);
        continue;
      }
      const uint16_t zeroDt[8]{};
      const canpPacket_t packet = canpMakePacket(messageId, dlc, data.data(), zeroDt);
      const auto payload = encodeDashboardBatch(&packet, 1, steadyTimestampMs(), sequence);
      if (!writeDashboardMessage(socket, DashboardLink::MessageType::CanWrite, sequence++,
                                 payload.data(), static_cast<uint32_t>(payload.size()))) {
        publishError(txBuffer, "Photon Dashboard: CAN write transport failed");
        break;
      }
      // Preserve the PCAN behavior: Lists and monitors reflect an accepted local
      // DBC write immediately, even if the physical bus does not echo it.
      receiveCANFrame(messageId, dlc, data.data(), steadyTimestampMs(), cache, arena, frameEvents,
                      true);
    }

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(socket, &readSet);
    timeval timeout{};
    timeout.tv_usec = 10000;
    const int ready = select(selectSocketCount(socket), &readSet, nullptr, nullptr, &timeout);
    if (ready == SOCKET_ERROR) {
      publishError(txBuffer, "Photon Dashboard: " + socketError("TCP recv select"));
      break;
    }
    if (ready == 0 || !FD_ISSET(socket, &readSet)) continue;

    DashboardLink::Header header{};
    std::vector<uint8_t> payload{};
    if (!readDashboardMessage(socket, header, payload)) {
      publishError(txBuffer, "Photon Dashboard: invalid or closed relay connection");
      break;
    }
    const auto type = static_cast<DashboardLink::MessageType>(header.type);
    if (type == DashboardLink::MessageType::Telemetry) {
      canpBatch_t batch{};
      if (!decodeDashboardBatch(payload, batch)) {
        publishError(txBuffer, "Photon Dashboard: malformed telemetry batch");
        break;
      }
      for (uint16_t index = 0; index < batch.count; ++index) {
        const canpPacket_t& packet = batch.packets[index];
        receiveCANFrame(canpGetId(&packet), packet.dlc, packet.data, batch.timestamp, cache, arena,
                        frameEvents, false);
      }
    } else if (type == DashboardLink::MessageType::Status && payload.size() == 1) {
      const bool available = (payload[0] & DashboardLink::RemoteWritesEnabled) != 0;
      const bool granted = (payload[0] & DashboardLink::ControllerLeaseHeld) != 0;
      transmitAvailable.store(available, std::memory_order_release);
      controlsArmed.store(granted, std::memory_order_release);
      if (!available) armRequested.store(false, std::memory_order_release);
    } else if (type == DashboardLink::MessageType::ArmGranted) {
      controlsArmed.store(true, std::memory_order_release);
      transmitAvailable.store(true, std::memory_order_release);
      publishMessage(txBuffer, "Photon Dashboard: remote CAN controller armed");
    } else if (type == DashboardLink::MessageType::ArmDenied) {
      controlsArmed.store(false, std::memory_order_release);
      publishError(txBuffer, "Photon Dashboard: remote CAN controller unavailable");
    }
  }
  transmitAvailable.store(false, std::memory_order_release);
  controlsArmed.store(false, std::memory_order_release);
  closeSocket(socket);
  publishMessage(txBuffer, timeNow() + "Photon Dashboard stopped");
}

void Protocols::discoverDashboards(std::stop_token stoken,
                                   SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer) {
  std::string error{};
#ifdef _WIN32
  if (!ensureWinsock(error)) {
    publishError(txBuffer, "Photon Dashboard discovery: " + error);
    return;
  }
#endif
  SocketHandle socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket == INVALID_SOCKET) {
    publishError(txBuffer, "Photon Dashboard discovery: " + socketError("UDP socket"));
    return;
  }
  int broadcast = 1;
#ifdef _WIN32
  if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast),
                 sizeof(broadcast)) == SOCKET_ERROR) {
#else
  if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) == SOCKET_ERROR) {
#endif
    closeSocket(socket);
    publishError(txBuffer, "Photon Dashboard discovery: cannot enable broadcast");
    return;
  }
  sockaddr_in destination{};
  destination.sin_family = AF_INET;
  destination.sin_port = htons(DashboardLink::kDiscoveryPort);
  destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  DashboardLink::DiscoveryRequest request{.magic = htonl(DashboardLink::kMagic),
                                           .version = htons(DashboardLink::kVersion),
                                           .reserved = 0};
  sendto(socket, reinterpret_cast<const char*>(&request), sizeof(request), 0,
         reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));

  std::vector<std::string> devices{};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(900);
  while (!stoken.stop_requested() && std::chrono::steady_clock::now() < deadline) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(socket, &readSet);
    timeval timeout{};
    timeout.tv_usec = 100000;
    const int ready = select(selectSocketCount(socket), &readSet, nullptr, nullptr, &timeout);
    if (ready <= 0 || !FD_ISSET(socket, &readSet)) continue;
    DashboardLink::DiscoveryResponse response{};
    sockaddr_in source{};
#ifdef _WIN32
    int sourceBytes = sizeof(source);
#else
    socklen_t sourceBytes = sizeof(source);
#endif
    const int bytes = recvfrom(socket, reinterpret_cast<char*>(&response), sizeof(response), 0,
                               reinterpret_cast<sockaddr*>(&source), &sourceBytes);
    if (bytes != sizeof(response) || ntohl(response.magic) != DashboardLink::kMagic ||
        ntohs(response.version) != DashboardLink::kVersion)
      continue;
    char address[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &source.sin_addr, address, sizeof(address))) continue;
    const std::string name(response.name.data(), strnlen(response.name.data(), response.name.size()));
    const std::string entry = name + "|" + address + "|" +
                              std::to_string(ntohs(response.streamPort)) + "|" +
                              std::to_string(response.statusFlags);
    if (std::find(devices.begin(), devices.end(), entry) == devices.end()) devices.push_back(entry);
  }
  closeSocket(socket);
  txBuffer.write([devices = std::move(devices)](ProtocolReceiveVariant& out) {
    out = ProtocolDeviceList{.devices = std::move(devices)};
  });
}
