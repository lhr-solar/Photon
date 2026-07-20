#include "network.hpp"

#include <chrono>
#include <cmath>
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
  if (config.useAwsExtendedTelemetryDBC && (!parse || !parse->loadDBC(DBCType::HighNoonAWS))) {
    guiTxCommandBuffer.write([](ProtocolReceiveVariant& message) {
      message = ProtocolError{.error = "Cannot load the AWS extended telemetry DBC"};
    });
    return;
  }
  activePCANConfig.reset();
  activeDashboardConfig.reset();
  dashboardArmRequested.store(false, std::memory_order_release);
  activeTCPConfig = config;
  writerThread = std::jthread([this, config](std::stop_token stoken) {
    Protocols::TCP(stoken, guiTxCommandBuffer, canFrameBuffer, config, parse->arena,
                   timelineCursor);
  });
}

void Network::startPCAN(PCANConfig config) {
  std::lock_guard lock(writerMutex);
  stopWriterUnlocked();
  activeTCPConfig.reset();
  activeDashboardConfig.reset();
  activePCANConfig = config;
  pcanTransmitEnabled.store(!config.listenOnly, std::memory_order_release);
  canControlsEnabled.store(false, std::memory_order_release);
  writerThread = std::jthread([this, config](std::stop_token stoken) {
    Protocols::PCAN(stoken, guiTxCommandBuffer, canWriteBuffer, canFrameBuffer, config,
                    parse->arena);
  });
}

void Network::startDashboard(DashboardConfig config) {
  std::lock_guard lock(writerMutex);
  stopWriterUnlocked();
  if (!parse || !parse->loadDBC(DBCType::HighNoonTelemetry)) {
    guiTxCommandBuffer.write([](ProtocolReceiveVariant& message) {
      message = ProtocolError{.error = "Cannot load the normal car DBC for Photon Dashboard"};
    });
    return;
  }
  activeTCPConfig.reset();
  activePCANConfig.reset();
  activeDashboardConfig = config;
  pcanTransmitEnabled.store(false, std::memory_order_release);
  canControlsEnabled.store(false, std::memory_order_release);
  dashboardArmRequested.store(false, std::memory_order_release);
  writerThread = std::jthread([this, config](std::stop_token stoken) {
    Protocols::Dashboard(stoken, guiTxCommandBuffer, canWriteBuffer, canFrameBuffer, config,
                         parse->arena, dashboardArmRequested, pcanTransmitEnabled,
                         canControlsEnabled);
  });
}

void Network::discoverDashboards() {
  if (discoveryThread.joinable()) {
    discoveryThread.request_stop();
    discoveryThread.join();
  }
  discoveryThread = std::jthread([this](std::stop_token stoken) {
    Protocols::discoverDashboards(stoken, guiTxCommandBuffer);
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
  if (activeDashboardConfig) {
    dashboardArmRequested.store(armed, std::memory_order_release);
    if (!armed) canControlsEnabled.store(false, std::memory_order_release);
    return;
  }
  canControlsEnabled.store(armed && pcanTransmitEnabled.load(std::memory_order_acquire),
                           std::memory_order_release);
}

void Network::requestTimeline(uint16_t command, double seconds) {
  if (!std::isfinite(seconds) || seconds < 0.0) return;
  timelineCursor.request.store(
      TimelineCursorMailbox::pack(command, static_cast<uint64_t>(std::llround(seconds * 1000.0))),
      std::memory_order_relaxed);
  timelineCursor.sequence.fetch_add(1, std::memory_order_release);
  timelineCursor.sequence.notify_one();
}

void Network::stopWriter() {
  std::lock_guard lock(writerMutex);
  stopWriterUnlocked();
  activeTCPConfig.reset();
  activePCANConfig.reset();
  activeDashboardConfig.reset();
  dashboardArmRequested.store(false, std::memory_order_release);
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
      Protocols::TCP(stoken, guiTxCommandBuffer, canFrameBuffer, config, parse->arena,
                     timelineCursor);
    });
  } else if (activePCANConfig) {
    const PCANConfig config = *activePCANConfig;
    pcanTransmitEnabled.store(!config.listenOnly, std::memory_order_release);
    writerThread = std::jthread([this, config](std::stop_token stoken) {
      Protocols::PCAN(stoken, guiTxCommandBuffer, canWriteBuffer, canFrameBuffer, config,
                      parse->arena);
    });
  } else if (activeDashboardConfig) {
    const DashboardConfig config = *activeDashboardConfig;
    writerThread = std::jthread([this, config](std::stop_token stoken) {
      Protocols::Dashboard(stoken, guiTxCommandBuffer, canWriteBuffer, canFrameBuffer, config,
                           parse->arena, dashboardArmRequested, pcanTransmitEnabled,
                           canControlsEnabled);
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
      } else if (auto* dashboard = std::get_if<DashboardConfig>(cmd)) {
        startDashboard(*dashboard);
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
  if (discoveryThread.joinable()) {
    discoveryThread.request_stop();
    discoveryThread.join();
  }
  if (backendThread.joinable()) {
    backendThread.request_stop();
    backendThread.join();
  }
};
