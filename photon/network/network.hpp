/*[ξ] the photon network interface*/
#pragma once
#include <array>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>
#include "spsc.hpp"

class Network {
private:
    struct sample {
        sample() = default;
        sample(const sample&) = delete;
        sample& operator=(const sample&) = delete;
        sample(sample&&) = delete;
        sample& operator=(sample&&) = delete;

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

public:
    Network();
    void producer();
    void parser();

    bool readSample(uint16_t canId, uint64_t& outValue);
    void writeSample(uint16_t canId, uint64_t value);

    SPSCQueue<uint8_t> spscQueue;
    std::string IP = "3.141.38.115";
    unsigned PORT = 8187;

private:
    sample& ensureSample(uint16_t canId);
    bool decodeFrame(const std::string& frame, uint16_t& canId, uint64_t& value);
    void handleFrame(const std::string& frame);

    // DBC system
    DbcMessage& ensureDBC(uint32_t canId);
    void writeDBC(uint32_t canId, const std::string& name, uint8_t dlc, const std::string& sender);
    void writeSignal(uint32_t canId, const DbcSignal& sig);
    void handleDBCframe(const std::string& frame);
    bool decodeDBCFrame(const std::string& frame, uint32_t& canId, std::string& name, uint8_t& dlc, std::string& sender);
    bool decodeSIGFrame(const std::string& frame, uint32_t& canId, DbcSignal& sig);

    std::mutex sampleMapMutex;
    std::unordered_map<uint16_t, std::unique_ptr<sample>> sampleMap;

    std::mutex dbcMapMutex;
    std::unordered_map<uint32_t, std::unique_ptr<DbcMessage>> dbcMap;

    uint32_t currentCanId = 0;
};
