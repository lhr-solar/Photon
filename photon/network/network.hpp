#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include "spsc.hpp"
#include "dbc_manager.hpp"
#include <deque>

// forward declaration
class TcpSocket;

class Network {
public:
    Network();

    // --- Threads ---
    void producer();
    void parser();

    // --- Debug / DBC ---
    void printDBCMap();
    DbcManager dbcManager;
    DbcManager& getDbcManager() { return dbcManager; }
    bool loadDBC(const std::string& path);

    // --- CAN sample interface ---
    bool readSample(uint16_t canId, uint64_t& outValue);
    void writeSample(uint16_t canId, uint64_t value);

    // --- Network configuration ---
    std::string IP = "127.0.0.1";//"3.141.38.115";
    unsigned PORT = 8187;
    SPSCQueue<uint8_t> spscQueue;

        struct DecodedEntry {
        uint16_t canId;
        uint64_t rawValue;
        std::vector<std::string> lines;   // e.g. "Temp=25°C, Mode=3, Enabled=1"
    };

    static constexpr size_t MAX_HISTORY = 100;
    std::mutex decodedHistoryMutex;
    std::deque<DecodedEntry> decodedHistory;

private:
    struct sample {
        std::mutex lock;
        uint64_t point = 0;
    };


    

    // --- Maps ---
    std::unordered_map<uint16_t, sample> sampleMap;
    std::mutex sampleMapMutex;;
    // --- Helpers ---
    sample& ensureSample(uint16_t canId);
    bool decodeFrame(const std::string& frame,
                     uint16_t& canId,
                     uint64_t& value);
    void handleFrame(const std::string& frame);
    void interpretDBCFrame(uint16_t canId, uint64_t rawValue, int dlc);
};
