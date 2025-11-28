/*[ξ] the photon network interface*/
#pragma once
#include <array>
#include <map>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>
#include "spsc.hpp"
#include "dbc.hpp"

class Network{
public:
    Network();
    ~Network();
    void producer();
    void parser();
    void uartProducer();
    void uartConsumer();

    bool readSample(uint16_t canId, uint64_t& outValue);
    void writeSample(uint16_t canId, uint64_t value);

    SPSCQueue<uint8_t> tcpQueue;
    SPSCQueue<uint8_t> uartQueue;
    std::string IP ="3.141.38.115";
    unsigned PORT = 8187;
    CanStore canStore;
    std::atomic<bool> running = true;

private:
    struct sample {
        sample() = default;
        sample(const sample&) = delete;
        sample& operator=(const sample&) = delete;
        sample(sample&&) = delete;
        sample& operator=(sample&&) = delete;

        std::mutex lock;
        int64_t point = 0;
        bool hasNew = false;
    };

    sample& ensureSample(uint16_t canId);
    bool decodeFrame(const std::string& frame, uint16_t& canId, uint64_t& value);
    void handleFrame(const std::string& frame);

    std::mutex sampleMapMutex;
    std::unordered_map<uint16_t, std::unique_ptr<sample>> sampleMap;

/* end of network class */
};
