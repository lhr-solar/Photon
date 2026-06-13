#include "protocols.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <thread>

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
#include <unistd.h>
using SocketHandle = int;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace {
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

void closeSocket(SocketHandle sock) {
  if (sock == INVALID_SOCKET) return;
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
}

bool waitForConnect(SocketHandle sock, std::stop_token stoken, std::string& error) {
  while (!stoken.stop_requested()) {
    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(sock, &writeSet);

    timeval timeout{};
    timeout.tv_usec = 100000;

    const int ready = select(static_cast<int>(sock + 1), nullptr, &writeSet, nullptr, &timeout);
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

  if (connect(sock, reinterpret_cast<const sockaddr*>(&server), sizeof(server)) == 0) return true;
  if (!wouldBlock()) {
    error = socketError("TCP connect");
    closeSocket(sock);
    sock = INVALID_SOCKET;
    return false;
  }

  if (waitForConnect(sock, stoken, error)) return true;

  closeSocket(sock);
  sock = INVALID_SOCKET;
  return false;
}
}  // namespace

typedef struct
{
    uint32_t can_id;
    uint8_t dlc;
    uint8_t data[8];
} __attribute__((packed)) can_packet_t;

void Protocols::TCP(std::stop_token stoken, SPMCQueue<ProtocolReceiveVariant, 32>& txBuffer,
                    TCPConfig config) {
  SocketHandle sock = INVALID_SOCKET;
  std::string error;
  if (!connectTcp(sock, config, stoken, error)) {
    if (!stoken.stop_requested()) publishError(txBuffer, error);
    return;
  }
  auto now = std::chrono::system_clock::now();
  std::string t = std::format("{:%H:%M:%S} : ", now);
  publishMessage(txBuffer,
                 t + "TCP connected");

  std::array<char, 8192> buffer{};
  while (!stoken.stop_requested()) {
    const int bytesRead = recv(sock, buffer.data(), static_cast<int>(buffer.size()), 0);
    if (bytesRead > 0) {
      //publishMessage(txBuffer, std::string(buffer.data(), static_cast<std::size_t>(bytesRead)));
   static std::vector<std::byte> pending;

    constexpr size_t packetSize = 13;

    auto* first = reinterpret_cast<const std::byte*>(buffer.data());
    pending.insert(pending.end(), first, first + bytesRead);

    size_t offset = 0;

    while (pending.size() - offset >= packetSize) {
      uint32_t canId{};
      uint64_t data{};

      std::memcpy(&canId, pending.data() + offset, sizeof(canId));
      std::memcpy(&data, pending.data() + offset + 5, sizeof(data));

      if constexpr (std::endian::native == std::endian::big) {
        canId = std::byteswap(canId);
        data = std::byteswap(data);
      }

      uint8_t dlc = std::to_integer<uint8_t>(pending[offset + 4]);
      dlc = std::min<uint8_t>(dlc, 8);

      canId &= 0x1FFFFFFF;

      publishMessage(
          txBuffer,
          std::format("CAN id=0x{:03X} dlc={} data=0x{:016X}", canId, dlc, data));

      offset += packetSize;
    }

    pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(offset));

      continue;
    }

    if (bytesRead == 0) {
      publishMessage(txBuffer, "TCP peer closed connection");
      break;
    }

    if (wouldBlock()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }

    publishError(txBuffer, socketError("TCP recv"));
    break;
  }

  closeSocket(sock);
  publishMessage(txBuffer, "TCP stopped");
}
