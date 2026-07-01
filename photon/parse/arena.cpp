#include "arena.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "../engine/include.hpp"
#include "imgui.h"

inline void progressCell(float fraction, const char* overlay = nullptr) {
  const float clamped = std::clamp(fraction, 0.0f, 1.0f);
  ImGui::ProgressBar(clamped, ImVec2(-FLT_MIN, 0.0f), overlay);
}

inline void tableRowText(const char* key, const char* value, float visual = -1.0f,
                         const char* overlay = nullptr) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::TextUnformatted(key);
  ImGui::TableSetColumnIndex(1);
  ImGui::TextUnformatted(value);
  ImGui::TableSetColumnIndex(2);
  if (visual >= 0.0f) progressCell(visual, overlay);
}

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

inline void formatBytesPerSecond(char* out, size_t outSize, double bytesPerSecond) {
  static constexpr std::array<const char*, 6> units{"B/s", "KB/s", "MB/s", "GB/s", "TB/s",
                                                   "PB/s"};
  double value = bytesPerSecond;
  size_t unit = 0;
  while (value >= 1024.0 && unit < units.size() - 1) {
    value /= 1024.0;
    unit++;
  }
  std::snprintf(out, outSize, "%.2f %s", value, units[unit]);
}

inline void formatPercent(char* out, size_t outSize, double fraction) {
  std::snprintf(out, outSize, "%.1f%%", std::clamp(fraction, 0.0, 1.0) * 100.0);
}

inline void formatAge(char* out, size_t outSize, double seconds) {
  if (seconds < 1.0) {
    std::snprintf(out, outSize, "%.0f ms", std::max(0.0, seconds) * 1000.0);
    return;
  }
  std::snprintf(out, outSize, "%.1f s", seconds);
}

inline void tableRowU64(const char* key, uint64_t value) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
  tableRowText(key, buf);
}

inline void tableRowBytes(const char* key, uint64_t bytes, float visual = -1.0f) {
  char buf[32];
  formatBytes(buf, sizeof(buf), bytes);
  tableRowText(key, buf, visual);
}

inline void tableRowPercent(const char* key, double fraction, float visual = -1.0f) {
  char buf[32];
  formatPercent(buf, sizeof(buf), fraction);
  tableRowText(key, buf, visual);
}

struct UiRing {
  std::array<float, 120> values{};
  int count{};
  int offset{};

  void reset() {
    values.fill(0.0f);
    count = 0;
    offset = 0;
  }

  void push(float value) {
    values[offset] = value;
    offset = (offset + 1) % static_cast<int>(values.size());
    if (count < static_cast<int>(values.size())) count++;
  }

  void plot(const char* id) const {
    if (count == 0) return;
    ImGui::PlotLines(id, values.data(), count, count == static_cast<int>(values.size()) ? offset : 0,
                     nullptr, FLT_MAX, FLT_MAX, ImVec2(-FLT_MIN, 28.0f));
  }
};

struct MessageUiStats {
  bool initialized{};
  uint32_t lastBytes{};
  uint32_t sampleCount{};
  size_t heldBytes{};
  double lastChangeTime{};
  double dataRate{};
  double dataTransfer{};
  double bandwidthFraction{};
  UiRing dataRateHistory{};
  UiRing dataTransferHistory{};

  void reset() {
    initialized = false;
    lastBytes = 0;
    sampleCount = 0;
    heldBytes = 0;
    lastChangeTime = 0.0;
    dataRate = 0.0;
    dataTransfer = 0.0;
    bandwidthFraction = 0.0;
    dataRateHistory.reset();
    dataTransferHistory.reset();
  }
};

struct ArenaUiFrameStats {
  size_t heldBytes{};
  double netDataRate{};
};

ArenaUiFrameStats refreshArenaUiStats(Arena& arena, std::array<MessageUiStats, MESSAGE_MAX>& cache) {
  ArenaUiFrameStats frame{};
  const double now = ImGui::GetTime();

  for (const uint32_t id : arena.validIds) {
    if (id >= arena.messages.size() || !arena.messages[id]) continue;

    Message& msg = *arena.messages[id];
    MessageUiStats& stats = cache[id];
    const uint32_t signalBytes = msg.signalSize.value.load(std::memory_order_acquire);
    stats.sampleCount = signalBytes / sizeof(double);
    stats.heldBytes =
        static_cast<size_t>(signalBytes) * (static_cast<size_t>(msg.signalCount) + 1);

    if (!stats.initialized || stats.lastBytes != signalBytes) {
      stats.initialized = true;
      stats.lastBytes = signalBytes;
      stats.lastChangeTime = now;
    }

    double timeSpan = 0.0;
    if (stats.sampleCount > 1 && msg.timeData) {
      const auto* times = static_cast<const double*>(msg.timeData);
      timeSpan = times[stats.sampleCount - 1] - times[0];
    }

    stats.dataTransfer = static_cast<double>(stats.heldBytes);
    stats.dataRate = timeSpan > 0.0 ? static_cast<double>(stats.heldBytes) / timeSpan : 0.0;
    frame.heldBytes += stats.heldBytes;
    frame.netDataRate += stats.dataRate;
  }

  for (const uint32_t id : arena.validIds) {
    if (id >= arena.messages.size() || !arena.messages[id]) continue;

    MessageUiStats& stats = cache[id];
    stats.bandwidthFraction = frame.netDataRate > 0.0 ? stats.dataRate / frame.netDataRate : 0.0;
    stats.dataRateHistory.push(static_cast<float>(stats.dataRate));
    stats.dataTransferHistory.push(static_cast<float>(stats.dataTransfer));
  }

  return frame;
}

inline size_t normalizeQuery(const char* src, char* dst, size_t dstSize) {
  size_t out = 0;
  for (size_t i = 0; src[i] != '\0' && out + 1 < dstSize; i++) {
    const unsigned char c = static_cast<unsigned char>(src[i]);
    if (std::isalnum(c)) dst[out++] = static_cast<char>(std::tolower(c));
  }
  dst[out] = '\0';
  return out;
}

inline bool normalizeText(const char* src, char* dst, size_t dstSize) {
  size_t out = 0;
  for (size_t i = 0; src[i] != '\0' && out + 1 < dstSize; i++) {
    const unsigned char c = static_cast<unsigned char>(src[i]);
    if (std::isalnum(c)) dst[out++] = static_cast<char>(std::tolower(c));
  }
  dst[out] = '\0';
  return out > 0;
}

inline bool containsQuery(const char* text, const char* query, size_t queryLen) {
  if (queryLen == 0) return true;

  char normalized[256];
  if (!normalizeText(text, normalized, sizeof(normalized))) return false;
  return std::strstr(normalized, query) != nullptr;
}

inline bool idMatchesQuery(uint32_t id, const char* query, size_t queryLen) {
  if (queryLen == 0) return true;

  char idText[32];
  std::snprintf(idText, sizeof(idText), "%u", id);
  if (containsQuery(idText, query, queryLen)) return true;

  std::snprintf(idText, sizeof(idText), "%x", id);
  return containsQuery(idText, query, queryLen);
}

bool messageMatchesQuery(const Message& msg, const char* query, size_t queryLen) {
  if (queryLen == 0) return true;
  if (idMatchesQuery(msg.id, query, queryLen)) return true;
  if (containsQuery(msg.name.c_str(), query, queryLen)) return true;
  if (containsQuery(msg.transmitter.c_str(), query, queryLen)) return true;

  for (size_t s = 0; s < msg.signalCount; s++) {
    if (msg.signals[s] && containsQuery(msg.signals[s]->name.c_str(), query, queryLen)) return true;
  }
  return false;
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

void Arena::statusUI(ImGuiWindowFlags flags) {
  if (ImGui::Begin("Arena Status", nullptr, flags)) {
    static std::array<MessageUiStats, MESSAGE_MAX> uiStats{};
    static UiRing netDataRateHistory{};
    static uint64_t cachedArenaGeneration = UINT64_MAX;

    if (cachedArenaGeneration != generation) {
      cachedArenaGeneration = generation;
      for (MessageUiStats& stats : uiStats) stats.reset();
      netDataRateHistory.reset();
    }

    const ArenaUiFrameStats frameStats = refreshArenaUiStats(*this, uiStats);
    netDataRateHistory.push(static_cast<float>(frameStats.netDataRate));
    char buf[64];

    ImGuiTableFlags summaryFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_SizingStretchProp |
                                   ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("arena_summary", 3, summaryFlags)) {
      ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 150.0f);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 140.0f);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();

      const size_t capacityBytes = bytesPerBuffer * totalBuffers;
      const float capacityFraction =
          arenaSize > 0 ? static_cast<float>(capacityBytes) / static_cast<float>(arenaSize) : 0.0f;
      const float heldFraction =
          capacityBytes > 0
              ? static_cast<float>(frameStats.heldBytes) / static_cast<float>(capacityBytes)
              : 0.0f;
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted("net bandwidth");
      ImGui::TableSetColumnIndex(1);
      formatBytesPerSecond(buf, sizeof(buf), frameStats.netDataRate);
      ImGui::TextUnformatted(buf);
      ImGui::TableSetColumnIndex(2);
      netDataRateHistory.plot("##arena_net_bandwidth_plot");
      tableRowBytes("arena size", arenaSize);
      tableRowBytes("buffer capacity", capacityBytes, capacityFraction);
      tableRowBytes("buffer usage", frameStats.heldBytes, heldFraction);
      tableRowBytes("fragmentation", arenaSize - capacityBytes, 1.0f - capacityFraction);
      tableRowU64("total pages", totalPages);
      tableRowBytes("bytes per signal", bytesPerBuffer);
      tableRowU64("total buffers", totalBuffers);
      tableRowU64("time buffers", totalTimeBuffers);
      tableRowU64("total signals", totalSignals);
      tableRowU64("points per signal", bytesPerBuffer / sizeof(double));
      ImGui::EndTable();
    }

    ImGui::SeparatorText("Messages");
    static char query[128]{};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##arena_search", "Search name, id, or signal", query, sizeof(query));
    char normalizedQuery[128];
    const size_t queryLen = normalizeQuery(query, normalizedQuery, sizeof(normalizedQuery));

    for (const uint32_t id : validIds) {
      if (id >= messages.size()) continue;
      Message* msg = messages[id];
      if (!msg) continue;
      if (!messageMatchesQuery(*msg, normalizedQuery, queryLen)) continue;

      MessageUiStats& stats = uiStats[id];
      char header[256];
      std::snprintf(header, sizeof(header), "0x%X  %.200s", msg->id, msg->name.c_str());

      ImGui::PushID(static_cast<int>(msg->id));
      if (ImGui::CollapsingHeader(header)) {
        if (ImGui::BeginTable("message_meta", 3, summaryFlags)) {
          ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 150.0f);
          ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 160.0f);
          ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
          ImGui::TableHeadersRow();
          std::snprintf(buf, sizeof(buf), "%u", msg->id);
          tableRowText("id", buf);
          tableRowText("name", msg->name.c_str());
          std::snprintf(buf, sizeof(buf), "%u", msg->dlc);
          tableRowText("dlc", buf);
          std::snprintf(buf, sizeof(buf), "%u", msg->signalCount);
          tableRowText("signal count", buf);
          const float fillFraction = bytesPerBuffer > 0 ? static_cast<float>(stats.lastBytes) /
                                                              static_cast<float>(bytesPerBuffer)
                                                        : 0.0f;
          tableRowU64("samples", stats.sampleCount);
          tableRowBytes("capacity used", stats.lastBytes, fillFraction);
          tableRowText("transmitter", msg->transmitter.c_str());
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted("data rate");
          ImGui::TableSetColumnIndex(1);
          formatBytesPerSecond(buf, sizeof(buf), stats.dataRate);
          ImGui::TextUnformatted(buf);
          ImGui::TableSetColumnIndex(2);
          stats.dataRateHistory.plot("##rate_plot");
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted("data transfer");
          ImGui::TableSetColumnIndex(1);
          formatBytes(buf, sizeof(buf), static_cast<uint64_t>(stats.dataTransfer));
          ImGui::TextUnformatted(buf);
          ImGui::TableSetColumnIndex(2);
          stats.dataTransferHistory.plot("##transfer_plot");
          tableRowPercent("bandwidth %", stats.bandwidthFraction,
                          static_cast<float>(stats.bandwidthFraction));
          ImGui::EndTable();
        }

        ImGuiTableFlags signalFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingFixedFit |
                                      ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_PadOuterX;
        if (ImGui::BeginTable("signals", 14, signalFlags)) {
          ImGui::TableSetupScrollFreeze(0, 1);
          ImGui::TableSetupColumn("Idx");
          ImGui::TableSetupColumn("Name");
          ImGui::TableSetupColumn("Start");
          ImGui::TableSetupColumn("Len");
          ImGui::TableSetupColumn("Endian");
          ImGui::TableSetupColumn("Type");
          ImGui::TableSetupColumn("Signed");
          ImGui::TableSetupColumn("Scale");
          ImGui::TableSetupColumn("Offset");
          ImGui::TableSetupColumn("Min");
          ImGui::TableSetupColumn("Max");
          ImGui::TableSetupColumn("Unit");
          ImGui::TableSetupColumn("Receiver");
          ImGui::TableSetupColumn("Age");
          ImGui::TableHeadersRow();
          for (size_t s{0uz}; s < msg->signalCount; s++) {
            Signal* sig = msg->signals[s];
            if (!sig) continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%zu", s);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(sig->name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", sig->startBit);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", sig->length);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", sig->endianness);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%d", sig->type);
            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(sig->isSigned ? "true" : "false");
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%.3f", sig->scale);
            ImGui::TableSetColumnIndex(8);
            ImGui::Text("%.3f", sig->offset);
            ImGui::TableSetColumnIndex(9);
            ImGui::Text("%.3f", sig->min);
            ImGui::TableSetColumnIndex(10);
            ImGui::Text("%.3f", sig->max);
            ImGui::TableSetColumnIndex(11);
            ImGui::TextUnformatted(sig->unit.c_str());
            ImGui::TableSetColumnIndex(12);
            ImGui::TextUnformatted(sig->receiver.c_str());
            ImGui::TableSetColumnIndex(13);
            formatAge(buf, sizeof(buf), ImGui::GetTime() - stats.lastChangeTime);
            ImGui::TextUnformatted(buf);
          }
          ImGui::EndTable();
        }
      }
      ImGui::PopID();
    }
  }
  ImGui::End();
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

// clears the existing message
// if no message exists, simply returns
void Arena::clear(uint32_t id) {
  if (id >= messages.size() || !messages[id]) return;
  messages[id]->signalSize.value.store(0, std::memory_order_release);
};

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
  for (const auto& id : validIds) {
    clear(id);
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
    return;
  }
#ifdef _WIN32
  VirtualFree(pool, 0, MEM_RELEASE);
#else
  munmap(pool, arenaSize);
#endif
  pool = nullptr;
  arenaSize = 0;
}
