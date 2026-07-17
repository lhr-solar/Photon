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
  if (config.useAwsExtendedTelemetryDBC &&
      (!parse || !parse->loadDBC(DBCType::HighNoonAWS))) {
    guiTxCommandBuffer.write([](ProtocolReceiveVariant& message) {
      message = ProtocolError{.error = "Cannot load the AWS extended telemetry DBC"};
    });
    return;
  }
  activePCANConfig.reset();
  activeTCPConfig = config;
  writerThread = std::jthread([this, config](std::stop_token stoken) {
    Protocols::TCP(stoken, guiTxCommandBuffer, config, parse->arena);
  });
}

void Network::startPCAN(PCANConfig config) {
  std::lock_guard lock(writerMutex);
  stopWriterUnlocked();
  activeTCPConfig.reset();
  activePCANConfig = config;
  pcanTransmitEnabled.store(!config.listenOnly, std::memory_order_release);
  canControlsEnabled.store(false, std::memory_order_release);
  writerThread = std::jthread([this, config](std::stop_token stoken) {
    Protocols::PCAN(stoken, guiTxCommandBuffer, canWriteBuffer, canFrameBuffer, config,
                    parse->arena);
  });
}

bool Network::sendDBCSignal(std::string_view messageName, std::string_view signalName,
                            double physicalValue) {
  return sendDBCFrame(messageName,
                      {{.signalName = std::string(signalName), .physicalValue = physicalValue}});
}

bool Network::sendDBCFrame(std::string_view messageName, std::vector<CANSignalValue> values,
                           bool allowUnseenFrame) {
  if (!canSendCAN() || values.empty()) return false;
  canWriteBuffer.write([messageName = std::string(messageName), values = std::move(values),
                        allowUnseenFrame](CANFrameWrite& request) mutable {
    request = {.messageName = std::move(messageName),
               .values = std::move(values),
               .allowUnseenFrame = allowUnseenFrame};
  });
  return true;
}

void Network::armCanControls(bool armed) {
  canControlsEnabled.store(armed && pcanTransmitEnabled.load(std::memory_order_acquire),
                           std::memory_order_release);
}

void Network::stopWriter() {
  std::lock_guard lock(writerMutex);
  stopWriterUnlocked();
  activeTCPConfig.reset();
  activePCANConfig.reset();
  pcanTransmitEnabled.store(false, std::memory_order_release);
  canControlsEnabled.store(false, std::memory_order_release);
}

void Network::stopWriterUnlocked() {
  pcanTransmitEnabled.store(false, std::memory_order_release);
  canControlsEnabled.store(false, std::memory_order_release);
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
  } else if (activePCANConfig) {
    const PCANConfig config = *activePCANConfig;
    pcanTransmitEnabled.store(!config.listenOnly, std::memory_order_release);
    writerThread = std::jthread([this, config](std::stop_token stoken) {
      Protocols::PCAN(stoken, guiTxCommandBuffer, canWriteBuffer, canFrameBuffer, config,
                      parse->arena);
    });
  }
}

bool Network::switchDBC(DBCType kind) {
  std::lock_guard lock(writerMutex);
  const bool shouldRestart = writerThread.joinable();
  stopWriterUnlocked();
  canControlsEnabled.store(false, std::memory_order_release);
  const bool loaded = parse && parse->loadDBC(kind);
  if (shouldRestart) restartWriterUnlocked();
  return loaded;
}

bool Network::switchDBCFile(const std::string& path) {
  std::lock_guard lock(writerMutex);
  const bool shouldRestart = writerThread.joinable();
  stopWriterUnlocked();
  canControlsEnabled.store(false, std::memory_order_release);
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
        startPCAN(*pcan);
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
