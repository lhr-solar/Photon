#pragma once
#include <cstdint>
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
  void startCandump(PCANConfig config);
  void startDashboardRelay(DashboardConfig config);
  void stopWriter();
  bool switchDBC(DBCType kind);
  bool switchDBCFile(const std::string& path);

  // Local SocketCAN TX for kiosk Display_Status (and similar). Opens can0 on first use.
  bool openCanTx(const char* channel = "can0");
  void closeCanTx();
  bool sendRawCan(uint32_t id, uint8_t dlc, const uint8_t* data);

  Parse* parse;

  std::jthread backendThread{};
  std::jthread writerThread{};
  std::jthread dashboardThread{};
  std::mutex writerMutex{};
  std::optional<TCPConfig> activeTCPConfig{};
  std::optional<PCANConfig> activePCANConfig{};
  std::optional<DashboardConfig> activeDashboardConfig{};

  /* GUI Sends here, Network Reads here */
  SPMCQueue<ProtocolTransmitVariant, 32> guiRxCommandBuffer{};
  /* Network Sends here, GUI Reads here */
  SPMCQueue<ProtocolReceiveVariant, 32> guiTxCommandBuffer{};

  /* Network Sends Here, Writer Reads Here */
  SPMCQueue<ProtocolTransmitVariant, 32> writerRxCommandBuffer{};
  /* Writer Sends Here, Network Reads Here */
  SPMCQueue<ProtocolReceiveVariant, 32> writerTxCommandBuffer{};
  // Raw local-bus frames fan out to the CM5 relay without affecting Arena ingest.
  SPMCQueue<CANFrameEvent, 512> canFrameBuffer{};

 private:
  void stopWriterUnlocked();
  void restartWriterUnlocked();
  void closeCanTxUnlocked();

  std::mutex canTxMutex{};
  int canTxSocket = -1;
  std::string canTxChannel{};
  bool canTxLoggedFailure = false;
};
