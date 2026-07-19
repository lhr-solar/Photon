#include "protocols.hpp"

#include <array>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <thread>

#include "../parse/arena.hpp"
#include "canp.h"

#ifdef _WIN32
#include <Ws2tcpip.h>
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
    const uint64_t timestamp = timelineCursor.timestampMs.load(std::memory_order_relaxed);
    if (canpWriteTimelineSeek(sock, timestamp) != 1) break;
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

#include "printFailure.hpp"

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
    if (printFailure(sock, config.tag, error)) return true;
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
    if (printFailure(sock, config.tag, error)) return true;
    closeSocket(sock);
    sock = INVALID_SOCKET;
    return false;
  }

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
  uint64_t previousTimestamp = 0;
  bool haveTimestamp = false;
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
      if (haveTimestamp && batch.timestamp < previousTimestamp)
        for (uint32_t id : arena.validIds) arena.clear(id);
      previousTimestamp = batch.timestamp;
      haveTimestamp = true;
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
  timelineSender.request_stop();
  timelineCursor.sequence.fetch_add(1, std::memory_order_release);
  timelineCursor.sequence.notify_all();
  timelineSender.join();
  closeSocket(sock);
  publishMessage(txBuffer, timeNow() + "TCP stopped");
}
