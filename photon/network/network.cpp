#include "network.hpp"

#include <chrono>
#include <iostream>
#include <thread>
#include <variant>

#include "protocols.hpp"

void Network::init() {
  backendThread = std::jthread([this](std::stop_token stoken) { backend(stoken); });
};

void Network::startTCP(TCPConfig config) {
  stopWriter();
  writerThread = std::jthread([this, config](std::stop_token stoken) {
    Protocols::TCP(stoken, guiTxCommandBuffer, config, parse->arena);
  });
}

void Network::stopWriter() {
  if (!writerThread.joinable()) return;
  writerThread.request_stop();
  writerThread.join();
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
      } else if (std::get_if<BLEConfig>(cmd)) {
      } else if (std::get_if<WLANConfig>(cmd)) {
      }
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
