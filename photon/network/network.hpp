#pragma once
#include <stop_token>
#include <thread>
#include <variant>

#include "../parse/parse.hpp"
#include "../parse/spmc.hpp"
#include "protocols.hpp"

struct Network {
  void init();
  void destroy();
  void backend(std::stop_token stoken);
  void startTCP(TCPConfig config);
  void stopWriter();
  Parse* parse;

  std::jthread backendThread{};
  std::jthread writerThread{};

  /* GUI Sends here, Network Reads here */
  SPMCQueue<ProtocolTransmitVariant, 32> guiRxCommandBuffer{};
  /* Network Sends here, GUI Reads here */
  SPMCQueue<ProtocolReceiveVariant, 32> guiTxCommandBuffer{};

  /* Network Sends Here, Writer Reads Here */
  SPMCQueue<ProtocolTransmitVariant, 32> writerRxCommandBuffer{};
  /* Writer Sends Here, Network Reads Here */
  SPMCQueue<ProtocolReceiveVariant, 32> writerTxCommandBuffer{};
};
