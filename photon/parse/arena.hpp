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

enum datatype {
    vINT = 0,
    vFLOAT = 1,
    vDOUBLE = 2
};

struct arenaConfig {
    size_t arenaSize{};
    std::array<uint32_t, MESSAGE_MAX> signalCounts{};
    std::vector<uint32_t> validIds{};
};

struct Signal{
    int startBit = 0;
    int length = 0;
    int endianness = 0;
    datatype type = vINT;
    bool isSigned = false;
    double scale = 1.0;
    double offset = 0.0;
    double min = 0.0;
    double max = 0.0;
    std::string name = "NULL";
    std::string unit = "NULL";
    std::string receiver = "NULL";
    std::chrono::system_clock::time_point lastTimeMutated = std::chrono::system_clock::now();
    std::chrono::milliseconds timeSinceMutation{};
    void* data{};
};

struct Message{
    uint32_t id{};
    uint32_t dlc{};
    uint32_t signalCount{};
    std::string name{};
    std::string transmitter{};
    std::chrono::system_clock::time_point lastTimeUpdated = std::chrono::system_clock::now();
    std::chrono::milliseconds timeSinceUpdate{};
    double dataRate{};
    double dataTransfer{};
    double bandwidthPercentage{};
    alignas(64) std::atomic<uint32_t> signalSize{};
    std::array<Signal*, SIGNAL_MAX> signals{};
};

struct Arena{
    void* pool{};
    uint8_t* cursor{};
    size_t remaining{};
    size_t totalPages{};
    size_t pagesPerSignal{};
    size_t bytesPerSignal{};
    uint32_t arenaSize = {};
    uint32_t totalSignals = {};
    std::vector<uint32_t> validIds{};
    std::array<Message*, MESSAGE_MAX> messages{};

    void init(const arenaConfig& config);
    void* alloc(size_t bytes, size_t align);
    void read(uint32_t id, uint32_t signal, void** data, uint32_t* size);
    bool write(uint32_t id, uint32_t signal, void* data, uint32_t size);
    void clear(uint32_t signal);
    void destroy();
    std::vector<size_t> search(const std::string& query);

    void status();
    void statusUI();
};
