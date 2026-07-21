#include "network.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <variant>

#include "../engine/include.hpp"
#include "protocols.hpp"

#ifdef LINUX
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

void Network::init() {
  backendThread = std::jthread([this](std::stop_token stoken) { backend(stoken); });
};

void Network::startTCP(TCPConfig config) {
  std::lock_guard lock(writerMutex);
  stopWriterUnlocked();
  activeTCPConfig = config;
  writerThread = std::jthread([this, config](std::stop_token stoken) {
    Protocols::TCP(stoken, guiTxCommandBuffer, config, parse->arena);
  });
}

void Network::startCandump(PCANConfig config) {
#ifdef LINUX
  std::lock_guard lock(writerMutex);
  stopWriterUnlocked();
  activeTCPConfig.reset();
  activePCANConfig = config;
  writerThread = std::jthread([this, config](std::stop_token stoken) {
    Protocols::Candump(stoken, guiTxCommandBuffer, canFrameBuffer, config, parse->arena);
  });
#else
  (void)config;
#endif
}

void Network::startDashboardRelay(DashboardConfig config) {
#ifdef LINUX
  if (dashboardThread.joinable()) {
    dashboardThread.request_stop();
    dashboardThread.join();
  }
  activeDashboardConfig = config;
  dashboardThread = std::jthread([this, config](std::stop_token stoken) {
    Protocols::DashboardRelay(stoken, guiTxCommandBuffer, canFrameBuffer, config);
  });
#else
  (void)config;
#endif
}

void Network::stopWriter() {
  std::lock_guard lock(writerMutex);
  if (dashboardThread.joinable()) {
    dashboardThread.request_stop();
    dashboardThread.join();
  }
  stopWriterUnlocked();
  activeTCPConfig.reset();
  activePCANConfig.reset();
  activeDashboardConfig.reset();
}

void Network::stopWriterUnlocked() {
  if (!writerThread.joinable()) return;
  writerThread.request_stop();
  writerThread.join();
}

void Network::restartWriterUnlocked() {
  if (!parse) return;
  if (activeTCPConfig) {
    const TCPConfig config = *activeTCPConfig;
    writerThread = std::jthread([this, config](std::stop_token stoken) {
      Protocols::TCP(stoken, guiTxCommandBuffer, config, parse->arena);
    });
    return;
  }
#ifdef LINUX
  if (activePCANConfig) {
    const PCANConfig config = *activePCANConfig;
    writerThread = std::jthread([this, config](std::stop_token stoken) {
      Protocols::Candump(stoken, guiTxCommandBuffer, canFrameBuffer, config, parse->arena);
    });
  }
#endif
}

bool Network::switchDBC(DBCType kind) {
  std::lock_guard lock(writerMutex);
  const bool shouldRestart =
      writerThread.joinable() && (activeTCPConfig.has_value() || activePCANConfig.has_value());
  stopWriterUnlocked();
  const bool loaded = parse && parse->loadDBC(kind);
  if (shouldRestart) restartWriterUnlocked();
  return loaded;
}

bool Network::switchDBCFile(const std::string& path) {
  std::lock_guard lock(writerMutex);
  const bool shouldRestart =
      writerThread.joinable() && (activeTCPConfig.has_value() || activePCANConfig.has_value());
  stopWriterUnlocked();
  const bool loaded = parse && parse->loadDBCFile(path);
  if (shouldRestart) restartWriterUnlocked();
  return loaded;
}

void Network::backend(std::stop_token stoken) {
  auto reader = guiRxCommandBuffer.getReader();
  while (!stoken.stop_requested()) {
    auto cmd = reader.readLast();
    if (cmd != NULL) {
      if (auto* tcp = std::get_if<TCPConfig>(cmd)) {
        startTCP(*tcp);
      } else if (auto* udp = std::get_if<UDPConfig>(cmd)) {
      } else if (auto* uart = std::get_if<UARTConfig>(cmd)) {
      } else if (auto* pcan = std::get_if<PCANConfig>(cmd)) {
        startCandump(*pcan);
      } else if (std::get_if<BLEConfig>(cmd)) {
      } else if (std::get_if<WLANConfig>(cmd)) {
      } else if (std::get_if<Quit>(cmd))
        stopWriter();
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    };
  };
  stopWriter();
};

void Network::destroy() {
  closeCanTx();
  if (backendThread.joinable()) {
    backendThread.request_stop();
    backendThread.join();
  }
  if (dashboardThread.joinable()) {
    dashboardThread.request_stop();
    dashboardThread.join();
  }
};

bool Network::openCanTx(const char* channel) {
#ifdef LINUX
  std::lock_guard lock(canTxMutex);
  if (canTxSocket >= 0 && canTxChannel == channel) return true;
  closeCanTxUnlocked();
  const int socketFd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (socketFd < 0) {
    if (!canTxLoggedFailure) {
      logs("[!] SocketCAN TX: socket failed: " << strerror(errno));
      canTxLoggedFailure = true;
    }
    return false;
  }
  ifreq request{};
  std::snprintf(request.ifr_name, sizeof(request.ifr_name), "%s", channel);
  if (ioctl(socketFd, SIOCGIFINDEX, &request) < 0) {
    if (!canTxLoggedFailure) {
      logs("[!] SocketCAN TX: interface lookup failed for " << channel << ": " << strerror(errno));
      canTxLoggedFailure = true;
    }
    ::close(socketFd);
    return false;
  }
  sockaddr_can address{};
  address.can_family = AF_CAN;
  address.can_ifindex = request.ifr_ifindex;
  if (bind(socketFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    if (!canTxLoggedFailure) {
      logs("[!] SocketCAN TX: bind failed for " << channel << ": " << strerror(errno));
      canTxLoggedFailure = true;
    }
    ::close(socketFd);
    return false;
  }
  canTxSocket = socketFd;
  canTxChannel = channel;
  canTxLoggedFailure = false;
  logs("[+] SocketCAN TX ready on " << channel);
  return true;
#else
  (void)channel;
  return false;
#endif
}

void Network::closeCanTxUnlocked() {
#ifdef LINUX
  if (canTxSocket >= 0) {
    ::close(canTxSocket);
    canTxSocket = -1;
  }
  canTxChannel.clear();
#else
#endif
}

void Network::closeCanTx() {
  std::lock_guard lock(canTxMutex);
  closeCanTxUnlocked();
}

bool Network::sendRawCan(uint32_t id, uint8_t dlc, const uint8_t* data) {
#ifdef LINUX
  if (dlc > 8 || data == nullptr || id > CAN_SFF_MASK) return false;
  std::lock_guard lock(canTxMutex);
  if (canTxSocket < 0) return false;
  can_frame frame{};
  frame.can_id = id;
  frame.can_dlc = dlc;
  std::memcpy(frame.data, data, dlc);
  if (::write(canTxSocket, &frame, sizeof(frame)) != static_cast<ssize_t>(sizeof(frame))) {
    if (!canTxLoggedFailure) {
      logs("[!] SocketCAN TX write failed: " << strerror(errno));
      canTxLoggedFailure = true;
    }
    ::close(canTxSocket);
    canTxSocket = -1;
    canTxChannel.clear();
    return false;
  }
  return true;
#else
  (void)id;
  (void)dlc;
  (void)data;
  return false;
#endif
}

