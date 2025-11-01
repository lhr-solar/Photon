/* [ξ] Photon Network Interface
   Handles real-time SLCAN (CAN over TCP) parsing and DBC mapping.
   Designed for thread-safe CAN streaming and modular DBC integration.
*/

#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include "spsc.hpp"

// forward declaration
class TcpSocket;
class DbcConnector;

class Network {
public:
    Network();

    // --- Threads ---
    void producer();
    void parser();

    // --- Debug / DBC ---
    void printDBCMap();
    bool loadDBC(const std::string& path);

    // --- CAN sample interface ---
    bool readSample(uint16_t canId, uint64_t& outValue);
    void writeSample(uint16_t canId, uint64_t value);

    // --- Network configuration ---
    std::string IP = "3.141.38.115";
    unsigned PORT = 8187;
    SPSCQueue<uint8_t> spscQueue;

private:
    struct sample {
        std::mutex lock;
        uint64_t point = 0;
    };

    struct DbcSignal {
        std::string name;
        int startBit = 0;
        int length = 0;
        int byteOrder = 0;
        bool isSigned = false;
        double scale = 1.0;
        double offset = 0.0;
        double minVal = 0.0;
        double maxVal = 0.0;
    };

    struct DbcMessage {
        std::string name;
        uint8_t dlc = 0;
        std::string sender;
        std::vector<DbcSignal> signals;
    };

    // --- Maps ---
    std::unordered_map<uint16_t, sample> sampleMap;
    std::unordered_map<uint32_t, DbcMessage> dbcMap;

    std::mutex sampleMapMutex;
    std::mutex dbcMapMutex;

    uint32_t currentCanId = 0;

    // --- Helpers ---
    sample& ensureSample(uint16_t canId);
    bool decodeFrame(const std::string& frame, uint16_t& canId, uint64_t& value);
    void handleFrame(const std::string& frame);
};
