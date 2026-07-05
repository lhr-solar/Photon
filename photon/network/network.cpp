#include "network.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <variant>

#include "protocols.hpp"

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
    Protocols::Candump(stoken, guiTxCommandBuffer, config, parse->arena);
  });
#else
  (void)config;
#endif
}

void Network::stopWriter() {
  std::lock_guard lock(writerMutex);
  stopWriterUnlocked();
  activeTCPConfig.reset();
  activePCANConfig.reset();
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
      Protocols::Candump(stoken, guiTxCommandBuffer, config, parse->arena);
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
  if (backendThread.joinable()) {
    backendThread.request_stop();
    backendThread.join();
  }
};
