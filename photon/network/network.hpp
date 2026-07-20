#pragma once
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
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
  void requestTimeline(uint16_t command, double seconds = 0.0);
  void stopWriter();
  bool switchDBC(DBCType kind);
  bool switchDBCFile(const std::string& path);
  Parse* parse;

  std::jthread backendThread{};
  std::jthread writerThread{};
  std::mutex writerMutex{};
  std::optional<TCPConfig> activeTCPConfig{};
  TimelineCursorMailbox timelineCursor{};

  /* GUI Sends here, Network Reads here */
  SPMCQueue<ProtocolTransmitVariant, 32> guiRxCommandBuffer{};
  /* Network Sends here, GUI Reads here */
  SPMCQueue<ProtocolReceiveVariant, 32> guiTxCommandBuffer{};

  /* Network Sends Here, Writer Reads Here */
  SPMCQueue<ProtocolTransmitVariant, 32> writerRxCommandBuffer{};
  /* Writer Sends Here, Network Reads Here */
  SPMCQueue<ProtocolReceiveVariant, 32> writerTxCommandBuffer{};

 private:
  void stopWriterUnlocked();
  void restartWriterUnlocked();
};
