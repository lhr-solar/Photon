#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

constexpr uint32_t PAGE_SIZE = 4096;
constexpr uint32_t MESSAGE_MAX = 2048;
constexpr uint32_t SIGNAL_MAX = 32;

struct arenaConfig {
    uint32_t arenaSize{};
    uint32_t messageCount{};
    uint32_t signalCount{};
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
    std::vector<double> data{};
};

struct Message{
    uint32_t id{};
    std::vector<Signal> signals{};
    std::atomic<bool> running{true};
};

struct Arena{
    uint32_t arenaSize = PAGE_SIZE * MESSAGE_MAX * SIGNAL_MAX;
    std::array<Message*, MESSAGE_MAX> messages{};
    void init(const arenaConfig& config);
    void read(uint32_t message, uint32_t signal);
    void write(uint32_t message, uint32_t signal);
    void allocate(uint32_t message);
    void destroy(uint32_t message);
};
