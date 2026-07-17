#pragma once
#include <atomic>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "../parse/parse.hpp"
#include "../parse/spmc.hpp"
#include "protocols.hpp"

struct Network {
  void init();
  void destroy();
  void backend(std::stop_token stoken);
  void startTCP(TCPConfig config);
  void startPCAN(PCANConfig config);
  void stopWriter();
  bool sendDBCSignal(std::string_view messageName, std::string_view signalName,
                     double physicalValue);
  bool sendDBCFrame(std::string_view messageName, std::vector<CANSignalValue> values,
                    bool allowUnseenFrame = false);
  void armCanControls(bool armed);
  bool canTransmitCAN() const { return pcanTransmitEnabled.load(std::memory_order_acquire); }
  bool canControlsArmed() const { return canControlsEnabled.load(std::memory_order_acquire); }
  bool canSendCAN() const { return canTransmitCAN() && canControlsArmed(); }
  bool switchDBC(DBCType kind);
  bool switchDBCFile(const std::string& path);
  Parse* parse;

  std::jthread backendThread{};
  std::jthread writerThread{};
  std::mutex writerMutex{};
  std::optional<TCPConfig> activeTCPConfig{};
  std::optional<PCANConfig> activePCANConfig{};
  std::atomic<bool> pcanTransmitEnabled{false};
  std::atomic<bool> canControlsEnabled{false};

  /* GUI Sends here, Network Reads here */
  SPMCQueue<ProtocolTransmitVariant, 32> guiRxCommandBuffer{};
  /* Network Sends here, GUI Reads here */
  SPMCQueue<ProtocolReceiveVariant, 32> guiTxCommandBuffer{};

  /* Network Sends Here, Writer Reads Here */
  SPMCQueue<ProtocolTransmitVariant, 32> writerRxCommandBuffer{};
  /* Writer Sends Here, Network Reads Here */
  SPMCQueue<ProtocolReceiveVariant, 32> writerTxCommandBuffer{};
  SPMCQueue<CANFrameWrite, 64> canWriteBuffer{};
  // Read by the Custom View CAN monitor.  It contains both bus RX and frames
  // accepted for TX so the UI updates even when the bus does not echo writes.
  SPMCQueue<CANFrameEvent, 512> canFrameBuffer{};

 private:
  void stopWriterUnlocked();
  void restartWriterUnlocked();
};
