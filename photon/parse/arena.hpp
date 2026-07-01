#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"

constexpr uint32_t PAGE_SIZE = 4096;
constexpr uint32_t MESSAGE_MAX = 0x2000;
constexpr uint32_t SIGNAL_MAX = 32;
constexpr uint32_t MINIMUM_ARENA_SIZE = PAGE_SIZE * MESSAGE_MAX * SIGNAL_MAX;

enum datatype { vINT = 0, vFLOAT = 1, vDOUBLE = 2 };

struct arenaConfig {
  size_t arenaSize{};
  std::array<uint32_t, MESSAGE_MAX> signalCounts{};
  std::vector<uint32_t> validIds{};
};

struct Signal {
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
  void* data{};
};

struct alignas(64) PublishedSize {
  std::atomic<uint32_t> value{};
};

static_assert(sizeof(PublishedSize) == 64);
static_assert(alignof(PublishedSize) == 64);

struct Message {
  uint32_t id{};
  uint32_t dlc{};
  uint32_t signalCount{};
  std::string name{};
  std::string transmitter{};
  PublishedSize signalSize{};
  void* timeData{};
  std::array<Signal*, SIGNAL_MAX> signals{};
};

struct Arena {
  void* pool{};
  uint8_t* cursor{};
  size_t remaining{};
  size_t totalPages{};
  size_t pagesPerBuffer{};
  size_t bytesPerBuffer{};
  size_t arenaSize = {};
  uint32_t totalSignals = {};
  uint32_t totalTimeBuffers = {};
  uint32_t totalBuffers = {};
  uint64_t generation = {};
  std::vector<uint32_t> validIds{};
  std::array<Message*, MESSAGE_MAX> messages{};

  void init(const arenaConfig& config);
  void* alloc(size_t bytes, size_t align);
  void read(uint32_t id, uint32_t signal, void** data, uint32_t* size);
  bool write(uint32_t id, uint32_t signal, void* data, uint32_t size);
  void readTime(uint32_t id, void** data, uint32_t* size);
  bool writeTime(uint32_t id, void* data, uint32_t size);
  bool appendFrame(uint32_t id, double timeValue, const double* signalValues, uint32_t signalCount);
  void clear(uint32_t signal);
  void destroy();

  void status();
  void statusUI(ImGuiWindowFlags flags);
};
