/*[ξ] the photon network interface*/
#pragma once
#include <array>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include "spsc.hpp"

class Network{
private:

public:
    Network();
    void producer();
    void parser();

    bool readSample(uint16_t canId, uint64_t& outValue);
    void writeSample(uint16_t canId, uint64_t value);

    SPSCQueue<uint8_t> spscQueue;
    std::string IP ="3.141.38.115";
    unsigned PORT = 8187;


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

    sample& ensureSample(uint16_t canId);
    bool decodeFrame(const std::string& frame, uint16_t& canId, uint64_t& value);
    void handleFrame(const std::string& frame);

    std::mutex sampleMapMutex;
    std::unordered_map<uint16_t, std::unique_ptr<sample>> sampleMap;

/* end of network class */
};
