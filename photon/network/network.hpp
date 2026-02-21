#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>
#include <deque>
#include <atomic>
#include "spsc.hpp"
#include "dbc_manager.hpp"

// forward declaration
class TcpSocket;

class Network {
public:
    Network();
    ~Network();

    // --- Threads ---
    void producer();
    void parser();
    void uartProducer();
    void uartConsumer();
    void candumpParser();

    // --- Debug / DBC ---
    void printDBCMap();
    DbcManager dbcManager;
    DbcManager& getDbcManager() { return dbcManager; }
    bool loadDBC(const std::string& path);

    // --- CAN sample interface ---
    bool readSample(uint16_t canId, uint64_t& outValue);
    void writeSample(uint16_t canId, uint64_t value);

    // --- Network configuration ---
    std::string IP = "3.141.38.115";
    unsigned PORT = 9000;
    
    SPSCQueue<uint8_t> tcpQueue;
    SPSCQueue<uint8_t> uartQueue;
    std::atomic<bool> running = true;

    struct DecodedEntry {
        uint16_t canId;
        uint64_t rawValue;
        std::vector<std::string> lines;   // e.g. "Temp=25°C, Mode=3, Enabled=1"
    };

    static constexpr size_t MAX_HISTORY = 100;
    std::mutex decodedHistoryMutex;
    std::deque<DecodedEntry> decodedHistory;

    // --- Parsed Signal Storage ---
    std::mutex parsedSignalsMutex;
    std::unordered_map<std::string, double> parsedSignals;
    bool readParsedSignal(const std::string& sigName, double& outValue);

private:
    struct sample {
        std::mutex lock;
        int64_t point = 0;
        bool hasNew = false;
    };

    // --- Maps ---
    std::mutex sampleMapMutex;
    std::unordered_map<uint16_t, std::unique_ptr<sample>> sampleMap;

    // --- Helpers ---
    sample& ensureSample(uint16_t canId);
    bool decodeFrame(const std::string& frame,
                     uint16_t& canId,
                     uint64_t& value);
    void handleFrame(const std::string& frame);
    void interpretDBCFrame(uint16_t canId, uint64_t rawValue, int dlc);
};
