#include "arena.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "../engine/include.hpp"
#include "imgui.h"

inline std::string fmtb(uint64_t b) {
  static constexpr std::array<const char*, 6> u{"B", "KB", "MB", "GB", "TB", "PB"};
  double v = b;
  size_t i = 0;
  while (v >= 1024.0 && i < u.size() - 1) {
    v /= 1024.0;
    ++i;
  }
  std::ostringstream s;
  s << std::fixed << std::setprecision(2) << v << ' ' << u[i];
  return s.str();
}

inline std::string fmtbs(double bps) {
  static constexpr std::array<const char*, 6> u{"B/s", "KB/s", "MB/s", "GB/s", "TB/s", "PB/s"};
  double v = bps;
  size_t i = 0;
  while (v >= 1024.0 && i < u.size() - 1) {
    v /= 1024.0;
    ++i;
  }
  std::ostringstream s;
  s << std::fixed << std::setprecision(2) << v << ' ' << u[i];
  return s.str();
}

inline void plotCell(const std::vector<float>& values, const char* id) {
  if (values.empty()) return;
  ImGui::PlotLines(id, values.data(), static_cast<int>(values.size()), 0, nullptr, FLT_MAX, FLT_MAX,
                   ImVec2(-FLT_MIN, 28.0f));
}

inline void progressCell(float fraction, const char* overlay = nullptr) {
  const float clamped = std::clamp(fraction, 0.0f, 1.0f);
  ImGui::ProgressBar(clamped, ImVec2(-FLT_MIN, 0.0f), overlay);
}

inline void tableRow(const char* key, const std::string& value, float visual = -1.0f,
                     const char* overlay = nullptr) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::TextUnformatted(key);
  ImGui::TableSetColumnIndex(1);
  ImGui::TextUnformatted(value.c_str());
  ImGui::TableSetColumnIndex(2);
  if (visual >= 0.0f) progressCell(visual, overlay);
}

inline size_t validIdFingerprint(const std::vector<uint32_t>& validIds) {
  size_t fingerprint = validIds.size();
  for (const auto& id : validIds) fingerprint = (fingerprint * 131u) ^ id;
  return fingerprint;
}

inline std::string normalizeSearchText(std::string text) {
  std::string out;
  out.reserve(text.size());
  for (unsigned char c : text)
    if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
  return out;
}

inline int levenshteinDistance(const std::string& x, const std::string& y) {
  std::vector<int> prev(y.size() + 1);
  std::vector<int> cur(y.size() + 1);
  for (size_t j = 0; j <= y.size(); j++) prev[j] = static_cast<int>(j);
  for (size_t i = 1; i <= x.size(); i++) {
    cur[0] = static_cast<int>(i);
    for (size_t j = 1; j <= y.size(); j++) {
      const int cost = (x[i - 1] == y[j - 1]) ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
    }
    prev.swap(cur);
  }
  return prev[y.size()];
}

inline int searchDistance(std::string a, std::string b) {
  a = normalizeSearchText(std::move(a));
  b = normalizeSearchText(std::move(b));
  if (a.empty()) return 0;
  if (b.empty()) return std::numeric_limits<int>::max() / 4;
  if (b.find(a) != std::string::npos) return 0;

  const int n = static_cast<int>(a.size());
  const int m = static_cast<int>(b.size());
  if (n >= m) return levenshteinDistance(a, b);

  int best = std::numeric_limits<int>::max();
  for (size_t i = 0; i + a.size() <= b.size(); i++)
    best = std::min(best, levenshteinDistance(a, b.substr(i, a.size())));
  return best;
}

inline bool isSearchSpace(unsigned char c) { return std::isspace(c); }
inline bool isSearchDigit(unsigned char c) { return std::isdigit(c); }
inline bool isSearchHexDigit(unsigned char c) { return std::isxdigit(c); }

struct SearchMatch {
  size_t idx;
  int score;
};

struct SearchMatchLess {
  const std::vector<uint32_t>* validIds{};

  bool operator()(const SearchMatch& a, const SearchMatch& b) const {
    if (a.score != b.score) return a.score < b.score;
    return (*validIds)[a.idx] < (*validIds)[b.idx];
  }
};

void Arena::status() {
  logs("arena size        : " << fmtb(arenaSize));
  logs("total signals     : " << totalSignals);
  logs("time buffers      : " << totalTimeBuffers);
  logs("total buffers     : " << totalBuffers);
  logs("total pages       : " << totalPages);
  logs("bytes per buffer  : " << fmtb(bytesPerBuffer));
  logs("unused            : " << fmtb((arenaSize - (bytesPerBuffer * totalBuffers))));
  logs("points per buffer : " << bytesPerBuffer / sizeof(double));
  for (const auto& i : validIds) {
    Message* msg = messages[i];
    if (!msg) continue;
    logs("message id        : " << msg->id);
    logs("message name      : " << msg->name);
    logs("dlc               : " << msg->dlc);
    logs("signal count      : " << msg->signalCount);
    logs("signal size       : " << msg->signalSize.load(std::memory_order_acquire));
    logs("time ptr          : " << msg->timeData);
    logs("transmitter       : " << msg->transmitter);
    logs("data rate         : " << msg->dataRate);
    logs("data transfer     : " << msg->dataTransfer);
    logs("bandwidth %       : " << msg->bandwidthPercentage);
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
    ImGuiTableFlags summaryFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_SizingStretchProp |
                                   ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("arena_summary", 3, summaryFlags)) {
      ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 150.0f);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 140.0f);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();
      size_t heldBytes = 0;
      double netDataRate = 0.0;
      for (const uint32_t id : validIds) {
        if (id >= messages.size() || !messages[id]) continue;
        Message* msg = messages[id];
        const uint32_t signalBytes = msg->signalSize.load(std::memory_order_acquire);
        heldBytes += static_cast<size_t>(signalBytes) * (static_cast<size_t>(msg->signalCount) + 1);
        netDataRate += msg->dataRate;
      }
      static std::array<float, 120> netDataRateHistory{};
      static int netDataRateHistoryCount = 0;
      static int netDataRateHistoryOffset = 0;
      netDataRateHistory[netDataRateHistoryOffset] = static_cast<float>(netDataRate);
      netDataRateHistoryOffset =
          (netDataRateHistoryOffset + 1) % static_cast<int>(netDataRateHistory.size());
      if (netDataRateHistoryCount < static_cast<int>(netDataRateHistory.size()))
        netDataRateHistoryCount++;

      const size_t capacityBytes = bytesPerBuffer * totalBuffers;
      const float capacityFraction =
          arenaSize > 0 ? static_cast<float>(capacityBytes) / static_cast<float>(arenaSize) : 0.0f;
      const float heldFraction =
          capacityBytes > 0 ? static_cast<float>(heldBytes) / static_cast<float>(capacityBytes)
                            : 0.0f;
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted("net bandwidth");
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(fmtbs(netDataRate).c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::PlotLines("##arena_net_bandwidth_plot", netDataRateHistory.data(),
                       netDataRateHistoryCount,
                       netDataRateHistoryCount == static_cast<int>(netDataRateHistory.size())
                           ? netDataRateHistoryOffset
                           : 0,
                       nullptr, FLT_MAX, FLT_MAX, ImVec2(-FLT_MIN, 28.0f));
      tableRow("arena size", fmtb(arenaSize));
      tableRow("buffer capacity", fmtb(capacityBytes), capacityFraction, nullptr);
      tableRow("buffer usage", fmtb(heldBytes), heldFraction, nullptr);
      tableRow("fragmentation", fmtb(arenaSize - capacityBytes), 1.0f - capacityFraction, nullptr);
      tableRow("total pages", std::to_string(totalPages));
      tableRow("bytes per signal", fmtb(bytesPerBuffer));
      tableRow("total buffers", std::to_string(totalBuffers));
      tableRow("time buffers", std::to_string(totalTimeBuffers));
      tableRow("total signals", std::to_string(totalSignals));
      tableRow("points per signal", std::to_string(bytesPerBuffer / sizeof(double)));
      ImGui::EndTable();
    }
    ImGui::SeparatorText("Messages");
    static char query[128]{};
    static std::string cachedQuery{};
    static size_t cachedGeneration = 0;
    static std::vector<size_t> cachedMatches = search("");
    const size_t generation = validIdFingerprint(validIds);
    if (cachedGeneration != generation) {
      cachedGeneration = generation;
      cachedQuery = query;
      cachedMatches = search(cachedQuery);
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##arena_search", "Search name, id, or signal", query, sizeof(query));
    if (cachedQuery != query) {
      cachedQuery = query;
      cachedMatches = search(cachedQuery);
    }
    for (const auto& match : cachedMatches) {
      if (match >= validIds.size()) continue;
      const uint32_t id = validIds[match];
      Message* msg = messages[id];
      if (!msg) continue;
      std::ostringstream header;
      header << "0x" << std::hex << std::uppercase << msg->id << std::dec << "  " << msg->name
             << "##msg" << msg->id;
      std::string label = header.str();
      if (ImGui::CollapsingHeader(label.c_str())) {
        if (ImGui::BeginTable(("message_meta##" + std::to_string(msg->id)).c_str(), 3,
                              summaryFlags)) {
          ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 150.0f);
          ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 160.0f);
          ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
          ImGui::TableHeadersRow();
          tableRow("id", std::to_string(msg->id));
          tableRow("name", msg->name);
          tableRow("dlc", std::to_string(msg->dlc));
          tableRow("signal count", std::to_string(msg->signalCount));
          const uint32_t signalBytes = msg->signalSize.load(std::memory_order_acquire);
          const uint32_t sampleCount = signalBytes / sizeof(double);
          const float fillFraction = bytesPerBuffer > 0 ? static_cast<float>(signalBytes) /
                                                              static_cast<float>(bytesPerBuffer)
                                                        : 0.0f;
          tableRow("samples", std::to_string(sampleCount), fillFraction, nullptr);
          tableRow("capacity used", fmtb(signalBytes), fillFraction, nullptr);
          tableRow("transmitter", msg->transmitter);
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted("data rate");
          ImGui::TableSetColumnIndex(1);
          ImGui::TextUnformatted(fmtbs(msg->dataRate).c_str());
          ImGui::TableSetColumnIndex(2);
          plotCell(msg->dataRateHistory, ("##rate_plot_" + std::to_string(msg->id)).c_str());
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted("data transfer");
          ImGui::TableSetColumnIndex(1);
          ImGui::TextUnformatted(fmtb(static_cast<uint64_t>(msg->dataTransfer)).c_str());
          ImGui::TableSetColumnIndex(2);
          plotCell(msg->dataTransferHistory,
                   ("##transfer_plot_" + std::to_string(msg->id)).c_str());
          tableRow("bandwidth %", std::to_string(msg->bandwidthPercentage),
                   static_cast<float>(msg->bandwidthPercentage), nullptr);
          ImGui::EndTable();
        }
        ImGuiTableFlags signalFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingFixedFit |
                                      ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_PadOuterX;
        if (ImGui::BeginTable(("signals##" + std::to_string(msg->id)).c_str(), 14, signalFlags)) {
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
            ImGui::Text("%lld ms", static_cast<long long>(sig->timeSinceMutation.count()));
          }
          ImGui::EndTable();
        }
      }
    }
  }
  ImGui::End();
};

void Arena::init(const arenaConfig& config) {
  if (config.validIds.empty()) return;
  validIds = config.validIds;
  std::sort(validIds.begin(), validIds.end());
  for (const auto& m : messages)
    if (m) clear(m->id);
  arenaSize = MINIMUM_ARENA_SIZE;
  if (config.arenaSize > MINIMUM_ARENA_SIZE) arenaSize = config.arenaSize;
#ifdef _WIN32
  pool = VirtualAlloc(nullptr, arenaSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
  pool = mmap(nullptr, arenaSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
  if (pool == nullptr
#ifndef _WIN32
      || pool == MAP_FAILED
#endif
  )
    return;
  totalSignals = 0;
  totalTimeBuffers = 0;
  totalBuffers = 0;
  for (const auto& idx : validIds) {
    uint32_t count = config.signalCounts[idx];
    if (count > 32) count = 32;
    totalSignals += count;
    totalTimeBuffers += 1;
  };
  totalBuffers = totalSignals + totalTimeBuffers;
  if (totalBuffers == 0) return;
  cursor = static_cast<uint8_t*>(pool);
  remaining = arenaSize;
  totalPages = arenaSize / PAGE_SIZE;
  pagesPerBuffer = totalPages / totalBuffers;
  bytesPerBuffer = PAGE_SIZE * pagesPerBuffer;

  for (const auto& idx : validIds) {
    messages[idx] = new (Message);
    Message& msg = *messages[idx];
    msg.id = idx;
    msg.signalCount = config.signalCounts[idx];
    msg.signalSize.store(0, std::memory_order_relaxed);
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
  messages[id]->signalSize.store(0, std::memory_order_release);
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

  const uint32_t published = msg.signalSize.load(std::memory_order_acquire);
  if (data) *data = msg.signals[signal]->data;
  if (size) *size = published;
};

// thread safe write
// appends the data to the signal buffer
bool Arena::write(uint32_t id, uint32_t signal, void* data, uint32_t size) {
  if (id >= messages.size() || !messages[id] || !data) return false;
  Message& msg = *messages[id];
  if (signal >= msg.signalCount || !msg.signals[signal]) return false;

  const uint32_t offset = msg.signalSize.load(std::memory_order_relaxed);
  if (offset > bytesPerBuffer || size > bytesPerBuffer - offset) return false;

  auto* dst = static_cast<uint8_t*>(msg.signals[signal]->data) + offset;
  std::memcpy(dst, data, size);
  msg.signalSize.store(offset + size, std::memory_order_release);
  return true;
};

void Arena::readTime(uint32_t id, void** data, uint32_t* size) {
  if (data) *data = nullptr;
  if (size) *size = 0;
  if (id >= messages.size() || !messages[id]) return;

  Message& msg = *messages[id];
  const uint32_t published = msg.signalSize.load(std::memory_order_acquire);
  if (data) *data = msg.timeData;
  if (size) *size = published;
}

bool Arena::writeTime(uint32_t id, void* data, uint32_t size) {
  if (id >= messages.size() || !messages[id] || !data) return false;
  Message& msg = *messages[id];
  if (!msg.timeData) return false;

  const uint32_t offset = msg.signalSize.load(std::memory_order_relaxed);
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

  const uint32_t offset = msg.signalSize.load(std::memory_order_relaxed);
  if (offset > bytesPerBuffer || sizeof(double) > bytesPerBuffer - offset) return false;

  auto* timeDst = static_cast<uint8_t*>(msg.timeData) + offset;
  std::memcpy(timeDst, &timeValue, sizeof(double));

  for (uint32_t i = 0; i < signalCount; i++) {
    Signal* sig = msg.signals[i];
    if (!sig || !sig->data) return false;
    auto* dst = static_cast<uint8_t*>(sig->data) + offset;
    std::memcpy(dst, &signalValues[i], sizeof(double));
  }

  msg.signalSize.store(offset + sizeof(double), std::memory_order_release);
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
  totalDataTransfer = 1.0;
  cursor = nullptr;
  remaining = 0;
  totalPages = 0;
  pagesPerBuffer = 0;
  bytesPerBuffer = 0;
  if (pool == nullptr) return;
#ifdef _WIN32
  VirtualFree(pool, 0, MEM_RELEASE);
#else
  munmap(pool, arenaSize);
#endif
  pool = nullptr;
}

std::vector<size_t> Arena::search(const std::string& query) {
  std::vector<size_t> out;
  if (query.empty()) {
    out.resize(validIds.size());
    for (size_t i{0uz}; i < validIds.size(); i++) out[i] = i;
    return out;
  }

  std::string trimmed = query;
  trimmed.erase(std::remove_if(trimmed.begin(), trimmed.end(), isSearchSpace), trimmed.end());
  uint32_t parsedId{};
  bool hasParsedId = false;
  try {
    if (trimmed.size() > 2 && trimmed[0] == '0' && (trimmed[1] == 'x' || trimmed[1] == 'X')) {
      parsedId = static_cast<uint32_t>(std::stoul(trimmed, nullptr, 16));
      hasParsedId = true;
    } else if (!trimmed.empty() && std::all_of(trimmed.begin(), trimmed.end(), isSearchDigit)) {
      parsedId = static_cast<uint32_t>(std::stoul(trimmed, nullptr, 10));
      hasParsedId = true;
    } else if (!trimmed.empty() && std::all_of(trimmed.begin(), trimmed.end(), isSearchHexDigit)) {
      parsedId = static_cast<uint32_t>(std::stoul(trimmed, nullptr, 16));
      hasParsedId = true;
    }
  } catch (...) {
  }

  std::vector<SearchMatch> matches;
  matches.reserve(validIds.size());
  for (size_t i{0uz}; i < validIds.size(); i++) {
    Message* msg = messages[validIds[i]];
    if (!msg) continue;

    std::ostringstream hex;
    hex << std::uppercase << std::hex << msg->id;
    int score = std::min(
        {searchDistance(query, msg->name), searchDistance(query, msg->transmitter) + 2,
         searchDistance(query, std::to_string(msg->id)) + 1, searchDistance(query, hex.str()) + 1,
         searchDistance(query, "0x" + hex.str()) + 1});
    if (hasParsedId && parsedId == msg->id) score = -100;
    for (size_t s{0uz}; s < msg->signalCount; s++)
      if (msg->signals[s])
        score = std::min(score, searchDistance(query, msg->signals[s]->name) + 1);
    matches.push_back({i, score});
  }
  std::sort(matches.begin(), matches.end(), SearchMatchLess{.validIds = &validIds});
  out.reserve(matches.size());
  for (const auto& match : matches) out.push_back(match.idx);
  return out;
}
