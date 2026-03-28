#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>
#include "spmc.hpp"

constexpr uint32_t PAGE_SIZE = 4096;
constexpr uint32_t MESSAGE_MAX = 2048;
constexpr uint32_t SIGNAL_MAX = 32;
constexpr uint32_t MINIMUM_ARENA_SIZE = PAGE_SIZE * MESSAGE_MAX * SIGNAL_MAX;

struct arenaConfig {
    uint32_t arenaSize{};
    std::array<uint32_t, MESSAGE_MAX> dataLengths{};
    std::array<uint32_t, MESSAGE_MAX> signalCounts{};
    std::vector<uint32_t> validIds{};
};

enum datatype {
    vINT = 0,
    vFLOAT = 1,
    vDOUBLE = 2
};

struct Signal{
    datatype type{};
    void* data{};
};

struct Message{
    uint32_t id{};
    uint32_t dlc{};
    uint32_t signalCount{};
    std::array<Signal*, SIGNAL_MAX> signals{};
};

struct Arena{
    uint32_t arenaSize = {};
    uint32_t totalSignals = {};
    std::array<Message*, MESSAGE_MAX> messages{};
    void* pool{};
    void init(const arenaConfig& config);
    void read(uint32_t message, uint32_t signal, std::vector<double>& data);
    void write(uint32_t message, uint32_t signal, std::vector<double>& data);
    void allocate(uint32_t message);
    void destroy(uint32_t message);
};
