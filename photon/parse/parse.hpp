/*[λ] the photon parsing interface*/
#pragma once
#include <array>
#include <string>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <atomic>

#include "dbc.hpp"
#include "corsa.hpp"
#include "../network/spsc.hpp"

struct sample {
    sample() = default;
    sample(const sample&) = delete;
    sample& operator=(const sample&) = delete;
    sample(sample&&) = delete;
    sample& operator=(sample&&) = delete;

    std::mutex lock;
    std::array<uint8_t, 8> point;
    bool hasNew = false;
};

struct Parse{
private:

public:

    void parser(SPSCQueue<uint8_t>& queue);
    void acParser(SPSCQueue<RTCarInfo>& queue);
    void handleFrame(const std::string& frame);
    bool decodeFrame(const std::string& frame, uint16_t& canId, std::array<uint8_t,8>& value);
    void writeSample(uint16_t canId, std::array<uint8_t,8>value);
    sample& ensureSample(uint16_t canId);
    bool readSample(uint16_t canId, std::array<uint8_t,8>& outValue);

    std::mutex sampleMapMutex;
    std::unordered_map<uint16_t, std::unique_ptr<sample>> sampleMap;
    enum {
        TCP     = 0,
        UDP     = 1,
        SERIAL  = 2,
        LOCAL   = 3,
        CORSA   = 4,
    } backend = CORSA;
    CanStore canStore {};
    std::string currentDBC = {};
    std::atomic<bool> running {true};

/* end of parse class */
};
