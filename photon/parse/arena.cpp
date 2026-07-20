#include "arena.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "../engine/include.hpp"

inline void formatBytes(char* out, size_t outSize, uint64_t bytes) {
  static constexpr std::array<const char*, 6> units{"B", "KB", "MB", "GB", "TB", "PB"};
  double value = static_cast<double>(bytes);
  size_t unit = 0;
  while (value >= 1024.0 && unit < units.size() - 1) {
    value /= 1024.0;
    unit++;
  }
  std::snprintf(out, outSize, "%.2f %s", value, units[unit]);
}

void Arena::status() {
  char bytes[32];
  formatBytes(bytes, sizeof(bytes), arenaSize);
  logs("arena size        : " << bytes);
  logs("total signals     : " << totalSignals);
  logs("time buffers      : " << totalTimeBuffers);
  logs("total buffers     : " << totalBuffers);
  logs("total pages       : " << totalPages);
  formatBytes(bytes, sizeof(bytes), bytesPerBuffer);
  logs("bytes per buffer  : " << bytes);
  formatBytes(bytes, sizeof(bytes), arenaSize - (bytesPerBuffer * totalBuffers));
  logs("unused            : " << bytes);
  logs("points per buffer : " << bytesPerBuffer / sizeof(double));
  for (const auto& i : validIds) {
    Message* msg = messages[i];
    if (!msg) continue;
    logs("message id        : " << msg->id);
    logs("message name      : " << msg->name);
    logs("dlc               : " << msg->dlc);
    logs("signal count      : " << msg->signalCount);
    logs("signal size       : " << msg->signalSize.value.load(std::memory_order_acquire));
    logs("time ptr          : " << msg->timeData);
    logs("transmitter       : " << msg->transmitter);
    for (size_t s{0uz}; s < msg->signalCount; s++) {
      Signal* sig = msg->signals[s];
      if (!sig) continue;
      logs("  signal index    : " << s);
      logs("  name            : " << sig->name);
      logs("  start bit       : " << sig->startBit);
      logs("  length          : " << sig->length);
      logs("  endianness      : " << sig->endianness);
      logs("  type            : " << sig->type);
      logs("  signed          : " << sig->isSigned);
      logs("  scale           : " << sig->scale);
      logs("  offset          : " << sig->offset);
      logs("  min             : " << sig->min);
      logs("  max             : " << sig->max);
      logs("  unit            : " << sig->unit);
      logs("  receiver        : " << sig->receiver);
      logs("  data ptr        : " << sig->data);
    }
  }
};

void Arena::init(const arenaConfig& config) {
  if (config.validIds.empty()) return;

  std::vector<uint32_t> nextValidIds = config.validIds;
  std::sort(nextValidIds.begin(), nextValidIds.end());

  uint32_t nextTotalSignals = 0;
  uint32_t nextTotalTimeBuffers = 0;
  for (const auto& idx : nextValidIds) {
    uint32_t count = config.signalCounts[idx];
    if (count > 32) count = 32;
    nextTotalSignals += count;
    nextTotalTimeBuffers += 1;
  }
  const uint32_t nextTotalBuffers = nextTotalSignals + nextTotalTimeBuffers;
  if (nextTotalBuffers == 0) return;

  const size_t nextArenaSize = std::max(config.arenaSize, static_cast<size_t>(MINIMUM_ARENA_SIZE));
  const size_t nextTotalPages = nextArenaSize / PAGE_SIZE;
  const size_t nextPagesPerBuffer = nextTotalPages / nextTotalBuffers;
  if (nextPagesPerBuffer == 0) return;

  for (const auto& m : messages)
    if (m) clear(m->id);
#ifdef _WIN32
  pool = VirtualAlloc(nullptr, nextArenaSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
  pool = mmap(nullptr, nextArenaSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
  if (pool == nullptr
#ifndef _WIN32
      || pool == MAP_FAILED
#endif
  ) {
    pool = nullptr;
    return;
  }

  validIds = std::move(nextValidIds);
  arenaSize = nextArenaSize;
  totalSignals = nextTotalSignals;
  totalTimeBuffers = nextTotalTimeBuffers;
  totalBuffers = nextTotalBuffers;
  generation++;
  cursor = static_cast<uint8_t*>(pool);
  remaining = arenaSize;
  totalPages = nextTotalPages;
  pagesPerBuffer = nextPagesPerBuffer;
  bytesPerBuffer = PAGE_SIZE * pagesPerBuffer;

  for (const auto& idx : validIds) {
    messages[idx] = new (Message);
    Message& msg = *messages[idx];
    msg.id = idx;
    msg.signalCount = config.signalCounts[idx];
    msg.signalSize.value.store(0, std::memory_order_relaxed);
    if (msg.signalCount > 32) msg.signalCount = 32;
    msg.timeData = alloc(bytesPerBuffer, PAGE_SIZE);
    for (auto i{0uz}; i < msg.signalCount; i++) {
      msg.signals[i] = new (Signal);
      void* mem = alloc(bytesPerBuffer, PAGE_SIZE);
      msg.signals[i]->data = mem;
    };
  }
}

void* Arena::alloc(size_t bytes, size_t align) {
  void* p = cursor;
  if (!std::align(align, bytes, p, remaining)) return nullptr;
  cursor = static_cast<uint8_t*>(p) + bytes;
  remaining -= bytes;
  return p;
};

void Arena::beginRead() {
  for (;;) {
    while (resetting.load(std::memory_order_acquire)) resetting.wait(true);
    readers.fetch_add(1, std::memory_order_acquire);
    if (!resetting.load(std::memory_order_acquire)) return;
    if (readers.fetch_sub(1, std::memory_order_release) == 1) readers.notify_all();
  }
}

void Arena::endRead() {
  if (readers.fetch_sub(1, std::memory_order_release) == 1) readers.notify_all();
}

void Arena::beginReset() {
  while (resetting.exchange(true, std::memory_order_acq_rel)) resetting.wait(true);
  while (const uint32_t count = readers.load(std::memory_order_acquire)) readers.wait(count);
}

void Arena::endReset() {
  resetting.store(false, std::memory_order_release);
  resetting.notify_all();
}

void Arena::clear(uint32_t id) {
  beginReset();
  if (id < messages.size() && messages[id])
    messages[id]->signalSize.value.store(0, std::memory_order_release);
  endReset();
}

void Arena::clearAll() {
  beginReset();
  for (uint32_t id : validIds)
    if (id < messages.size() && messages[id])
      messages[id]->signalSize.value.store(0, std::memory_order_release);
  endReset();
}

// thread safe read
// returns a pointer of the signals buffer
// returns the current populated size of the buffer
void Arena::read(uint32_t id, uint32_t signal, void** data, uint32_t* size) {
  if (data) *data = nullptr;
  if (size) *size = 0;
  if (id >= messages.size() || !messages[id]) return;

  Message& msg = *messages[id];
  if (signal >= msg.signalCount || !msg.signals[signal]) return;

  const uint32_t published = msg.signalSize.value.load(std::memory_order_acquire);
  if (data) *data = msg.signals[signal]->data;
  if (size) *size = published;
};

// thread safe write
// appends the data to the signal buffer
bool Arena::write(uint32_t id, uint32_t signal, void* data, uint32_t size) {
  if (id >= messages.size() || !messages[id] || !data) return false;
  Message& msg = *messages[id];
  if (signal >= msg.signalCount || !msg.signals[signal]) return false;

  const uint32_t offset = msg.signalSize.value.load(std::memory_order_relaxed);
  if (offset > bytesPerBuffer || size > bytesPerBuffer - offset) return false;

  auto* dst = static_cast<uint8_t*>(msg.signals[signal]->data) + offset;
  std::memcpy(dst, data, size);
  msg.signalSize.value.store(offset + size, std::memory_order_release);
  return true;
};

void Arena::readTime(uint32_t id, void** data, uint32_t* size) {
  if (data) *data = nullptr;
  if (size) *size = 0;
  if (id >= messages.size() || !messages[id]) return;

  Message& msg = *messages[id];
  const uint32_t published = msg.signalSize.value.load(std::memory_order_acquire);
  if (data) *data = msg.timeData;
  if (size) *size = published;
}

bool Arena::writeTime(uint32_t id, void* data, uint32_t size) {
  if (id >= messages.size() || !messages[id] || !data) return false;
  Message& msg = *messages[id];
  if (!msg.timeData) return false;

  const uint32_t offset = msg.signalSize.value.load(std::memory_order_relaxed);
  if (offset > bytesPerBuffer || size > bytesPerBuffer - offset) return false;

  auto* dst = static_cast<uint8_t*>(msg.timeData) + offset;
  std::memcpy(dst, data, size);
  return true;
}

bool Arena::appendFrame(uint32_t id, double timeValue, const double* signalValues,
                        uint32_t signalCount) {
  if (id >= messages.size() || !messages[id] || !signalValues) return false;
  Message& msg = *messages[id];
  if (signalCount != msg.signalCount || !msg.timeData) return false;

  const uint32_t offset = msg.signalSize.value.load(std::memory_order_relaxed);
  if (offset > bytesPerBuffer || sizeof(double) > bytesPerBuffer - offset) return false;

  auto* timeDst = static_cast<uint8_t*>(msg.timeData) + offset;
  std::memcpy(timeDst, &timeValue, sizeof(double));

  for (uint32_t i = 0; i < signalCount; i++) {
    Signal* sig = msg.signals[i];
    if (!sig || !sig->data) return false;
    auto* dst = static_cast<uint8_t*>(sig->data) + offset;
    std::memcpy(dst, &signalValues[i], sizeof(double));
  }

  msg.signalSize.value.store(offset + sizeof(double), std::memory_order_release);
  return true;
}

void Arena::destroy() {
  beginReset();
  for (const auto& id : validIds) {
    if (id >= messages.size() || !messages[id]) continue;
    Message* msg = messages[id];
    for (size_t i = 0; i < msg->signalCount; ++i) {
      delete msg->signals[i];
      msg->signals[i] = nullptr;
    }
    delete msg;
    messages[id] = nullptr;
  }
  validIds.clear();
  totalSignals = 0;
  totalTimeBuffers = 0;
  totalBuffers = 0;
  generation++;
  cursor = nullptr;
  remaining = 0;
  totalPages = 0;
  pagesPerBuffer = 0;
  bytesPerBuffer = 0;
  if (pool == nullptr) {
    arenaSize = 0;
    endReset();
    return;
  }
#ifdef _WIN32
  VirtualFree(pool, 0, MEM_RELEASE);
#else
  munmap(pool, arenaSize);
#endif
  pool = nullptr;
  arenaSize = 0;
  endReset();
}
