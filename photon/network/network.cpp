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
    Protocols::TCP(stoken, guiTxCommandBuffer, config);
  });
}

void Network::stopWriter() {
  if (!writerThread.joinable()) return;
  writerThread.request_stop();
  writerThread.join();
}

/* Takes commands from Main Thread */
/* Dispatches appropriate Writer() thread */
void Network::backend(std::stop_token stoken) {
  auto reader = guiRxCommandBuffer.getReader();
  while (!stoken.stop_requested()) {
    auto cmd = reader.readLast();
    if (cmd != NULL) {
      if (auto* tcp = std::get_if<TCPConfig>(cmd)) {
        std::cout << "got tcp config:"
                  << " ip=" << tcp->ip << " port=" << tcp->port << std::endl;
        startTCP(*tcp);
      } else if (auto* udp = std::get_if<UDPConfig>(cmd)) {
        std::cout << "got udp config:"
                  << " ip=" << udp->ip << " port=" << udp->port
                  << " subscribeMessage=" << udp->subscribeMessage << std::endl;
      } else if (auto* uart = std::get_if<UARTConfig>(cmd)) {
        std::cout << "got uart config:"
                  << " device=" << uart->device << " baudRate=" << uart->baudRate << std::endl;
      } else if (auto* pcan = std::get_if<PCANConfig>(cmd)) {
        std::cout << "got pcan config:"
                  << " channel=" << pcan->channel << " bitrateKbps=" << pcan->bitrateKbps
                  << " samplePointPercent=" << pcan->samplePointPercent
                  << " prescaler=" << pcan->prescaler << " btr0=" << pcan->btr0
                  << " btr1=" << pcan->btr1 << " useBtr=" << pcan->useBtr
                  << " listenOnly=" << pcan->listenOnly << " busoffReset=" << pcan->busoffReset
                  << std::endl;
      } else if (std::get_if<BLEConfig>(cmd)) {
        std::cout << "got ble config: no fields" << std::endl;
      } else if (std::get_if<WLANConfig>(cmd)) {
        std::cout << "got wlan config: no fields" << std::endl;
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
  stopWriter();
};
