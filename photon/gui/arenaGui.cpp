#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "../parse/arena.hpp"
#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "uiComponents.hpp"

using ArenaPalette = PhotonUi::Palette;
using PhotonUi::colorU32;
using PhotonUi::mixColor;
using PhotonUi::withAlpha;

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
  static constexpr std::array<const char*, 6> units{"B/s", "KB/s", "MB/s", "GB/s", "TB/s", "PB/s"};
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
  if (seconds < 0.0) {
    std::snprintf(out, outSize, "no data");
    return;
  }
  if (seconds < 1.0) {
    std::snprintf(out, outSize, "%.0f ms", std::max(0.0, seconds) * 1000.0);
    return;
  }
  std::snprintf(out, outSize, "%.1f s", seconds);
}

struct UiRing {
  std::array<float, 960> values{};
  std::array<double, 960> times{};
  int count{};
  int offset{};

  void reset() {
    values.fill(0.0f);
    times.fill(0.0);
    count = 0;
    offset = 0;
  }

  void push(float value, double time) {
    values[offset] = value;
    times[offset] = time;
    offset = (offset + 1) % static_cast<int>(values.size());
    if (count < static_cast<int>(values.size())) count++;
  }

  float at(int index) const {
    if (count <= 0) return 0.0f;
    const int start = count == static_cast<int>(values.size()) ? offset : 0;
    return values[(start + index) % static_cast<int>(values.size())];
  }

  double timeAt(int index) const {
    if (count <= 0) return 0.0;
    const int start = count == static_cast<int>(times.size()) ? offset : 0;
    return times[(start + index) % static_cast<int>(times.size())];
  }

  float average(double now, double windowSeconds) const {
    if (count <= 0) return 0.0f;

    double total = 0.0;
    int samples = 0;
    const double minTime = now - windowSeconds;
    for (int i = 0; i < count; i++) {
      if (timeAt(i) < minTime) continue;
      total += at(i);
      samples++;
    }
    return samples > 0 ? static_cast<float>(total / static_cast<double>(samples)) : 0.0f;
  }

  void drawSparkline(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 color) const {
    if (count < 2) return;

    float lo = at(0);
    float hi = lo;
    for (int i = 1; i < count; i++) {
      const float v = at(i);
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
    const float span = std::max(hi - lo, 1.0f);
    const float w = max.x - min.x;
    const float h = max.y - min.y;
    ImVec2 prev(min.x, max.y - ((at(0) - lo) / span) * h);
    for (int i = 1; i < count; i++) {
      const float x = min.x + (static_cast<float>(i) / static_cast<float>(count - 1)) * w;
      const float y = max.y - ((at(i) - lo) / span) * h;
      const ImVec2 cur(x, y);
      draw->AddLine(prev, cur, color, 1.8f);
      prev = cur;
    }
  }
};

struct MessageUiStats {
  bool initialized{};
  uint32_t lastBytes{};
  uint32_t sampleCount{};
  size_t heldBytes{};
  double lastPollTime{};
  double lastChangeTime{};
  double dataRate{};
  double bandwidthFraction{};

  void reset() {
    initialized = false;
    lastBytes = 0;
    sampleCount = 0;
    heldBytes = 0;
    lastPollTime = 0.0;
    lastChangeTime = 0.0;
    dataRate = 0.0;
    bandwidthFraction = 0.0;
  }
};

struct ArenaUiFrameStats {
  size_t heldBytes{};
  double netDataRate{};
};

ArenaUiFrameStats refreshArenaUiStats(Arena& arena,
                                      std::array<MessageUiStats, MESSAGE_MAX>& cache) {
  ArenaUiFrameStats frame{};
  const double now = ImGui::GetTime();

  for (const uint32_t id : arena.validIds) {
    if (id >= arena.messages.size() || !arena.messages[id]) continue;

    Message& msg = *arena.messages[id];
    MessageUiStats& stats = cache[id];
    const uint32_t signalBytes = msg.signalSize.value.load(std::memory_order_acquire);
    stats.sampleCount = signalBytes / sizeof(double);
    stats.heldBytes = static_cast<size_t>(signalBytes) * (static_cast<size_t>(msg.signalCount) + 1);

    if (!stats.initialized) {
      stats.initialized = true;
      stats.lastBytes = signalBytes;
      stats.lastPollTime = now;
      stats.lastChangeTime = signalBytes > 0 ? now : 0.0;
      stats.dataRate = 0.0;
    } else {
      const uint32_t deltaBytes =
          signalBytes >= stats.lastBytes ? signalBytes - stats.lastBytes : signalBytes;
      const double elapsed = now - stats.lastPollTime;
      const size_t deltaHeldBytes =
          static_cast<size_t>(deltaBytes) * (static_cast<size_t>(msg.signalCount) + 1);
      stats.dataRate = elapsed > 0.0 ? static_cast<double>(deltaHeldBytes) / elapsed : 0.0;
      if (deltaBytes > 0 || signalBytes < stats.lastBytes) stats.lastChangeTime = now;
      stats.lastBytes = signalBytes;
      stats.lastPollTime = now;
    }

    frame.heldBytes += stats.heldBytes;
    frame.netDataRate += stats.dataRate;
  }

  for (const uint32_t id : arena.validIds) {
    if (id >= arena.messages.size() || !arena.messages[id]) continue;

    MessageUiStats& stats = cache[id];
    stats.bandwidthFraction = frame.netDataRate > 0.0 ? stats.dataRate / frame.netDataRate : 0.0;
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

void drawLivePanel(const char* id, const char* label, const char* value, const char* detail,
                   ImVec2 size, const ArenaPalette& palette, const UiRing* sparkline = nullptr,
                   float fraction = -1.0f) {
  ImGui::PushID(id);
  ImGui::InvisibleButton("panel", size);
  const bool hovered = ImGui::IsItemHovered();
  const ImGuiID itemId = ImGui::GetItemID();
  const float focus = iam_tween_float(itemId, ImHashStr("focus"), hovered ? 1.0f : 0.0f, 0.18f,
                                      iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade,
                                      ImGui::GetIO().DeltaTime);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec4 fill = mixColor(palette.panel, palette.raised, 0.34f + focus * 0.20f);
  draw->AddRectFilled(min, max, colorU32(withAlpha(fill, 0.90f)), 8.0f);
  draw->AddRect(min, max, colorU32(withAlpha(palette.border, 0.42f + focus * 0.20f)), 8.0f);
  draw->AddText({min.x + 14.0f, min.y + 10.0f}, colorU32(palette.muted), label);
  draw->AddText({min.x + 14.0f, min.y + 33.0f}, colorU32(palette.text), value);
  if (detail && detail[0] != '\0')
    draw->AddText({min.x + 14.0f, max.y - 38.0f}, colorU32(palette.muted), detail);

  if (sparkline) {
    const ImVec2 plotMin(min.x + 14.0f, min.y + 66.0f);
    const ImVec2 plotMax(max.x - 14.0f, max.y - 12.0f);
    draw->AddRectFilled(plotMin, plotMax, colorU32(withAlpha(palette.bg, 0.24f)), 6.0f);
    draw->PushClipRect(plotMin, plotMax, true);
    sparkline->drawSparkline(draw, plotMin, plotMax, colorU32(withAlpha(palette.accent, 0.84f)));
    draw->PopClipRect();
  }

  if (fraction >= 0.0f) {
    const float clamped = std::clamp(fraction, 0.0f, 1.0f);
    const ImVec2 barMin(min.x + 14.0f, max.y - 16.0f);
    const ImVec2 barMax(max.x - 14.0f, max.y - 10.0f);
    draw->AddRectFilled(barMin, barMax, colorU32(withAlpha(palette.border, 0.36f)), 2.0f);
    draw->AddRectFilled(barMin, {barMin.x + (barMax.x - barMin.x) * clamped, barMax.y},
                        colorU32(withAlpha(palette.accent, 0.82f)), 2.0f);
  }
  ImGui::PopID();
}

void drawStaticInfoStrip(float width, const ArenaPalette& palette, size_t capacityBytes,
                         size_t arenaSize, size_t bytesPerBuffer, size_t totalPages,
                         uint32_t totalBuffers, uint32_t totalTimeBuffers, uint32_t totalSignals,
                         size_t messageCount) {
  const float height = 54.0f;
  ImGui::InvisibleButton("static_info", {width, height});
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(min, max, colorU32(withAlpha(palette.panel, 0.62f)), 8.0f);
  draw->AddRect(min, max, colorU32(withAlpha(palette.border, 0.38f)), 8.0f);

  struct Item {
    const char* label;
    char value[40];
  };

  Item items[7] = {
      {"Pool", ""},
      {"Capacity per sample", ""},
      {"Capacity per signal", ""},
      {"Messages", ""},
      {"Signals", ""},
      {"Buffers", ""},
      {"Pages", ""},
  };
  formatBytes(items[0].value, sizeof(items[0].value), arenaSize);
  formatBytes(items[1].value, sizeof(items[1].value), capacityBytes);
  formatBytes(items[2].value, sizeof(items[2].value), bytesPerBuffer);
  std::snprintf(items[3].value, sizeof(items[3].value), "%llu",
                static_cast<unsigned long long>(messageCount));
  std::snprintf(items[4].value, sizeof(items[4].value), "%u", totalSignals);
  std::snprintf(items[5].value, sizeof(items[5].value), "%u data / %u time", totalBuffers,
                totalTimeBuffers);
  std::snprintf(items[6].value, sizeof(items[6].value), "%llu",
                static_cast<unsigned long long>(totalPages));

  const float cellW = (width - 24.0f) / 7.0f;
  for (int i = 0; i < 7; i++) {
    const float x = min.x + 12.0f + cellW * static_cast<float>(i);
    draw->PushClipRect({x, min.y}, {x + cellW - 12.0f, max.y}, true);
    draw->AddText({x, min.y + 8.0f}, colorU32(palette.muted), items[i].label);
    draw->AddText({x, min.y + 29.0f}, colorU32(palette.text), items[i].value);
    draw->PopClipRect();
    if (i > 0) {
      const float lineX = x - 10.0f;
      draw->AddLine({lineX, min.y + 10.0f}, {lineX, max.y - 10.0f},
                    colorU32(withAlpha(palette.border, 0.34f)));
    }
  }
}

void drawArenaHeader(float width, const ArenaPalette& palette, const ArenaUiFrameStats& frameStats,
                     const UiRing& netHistory, double smoothedNetDataRate, size_t capacityBytes,
                     float heldFraction, size_t arenaSize, size_t bytesPerBuffer, size_t totalPages,
                     uint32_t totalBuffers, uint32_t totalTimeBuffers, uint32_t totalSignals,
                     size_t messageCount) {
  const ImGuiStyle& style = ImGui::GetStyle();
  const float gap = style.ItemSpacing.x;
  const bool wide = width > 760.0f;
  const float liveH = 122.0f;
  const float netW = wide ? (width - gap) * 0.64f : width;
  const float storageW = wide ? width - netW - gap : width;
  char value[64];
  char detail[64];

  formatBytesPerSecond(value, sizeof(value), smoothedNetDataRate);
  drawLivePanel("net", "Bandwidth", value, "", {netW, liveH}, palette, &netHistory);
  if (wide) ImGui::SameLine(0.0f, gap);

  formatBytes(value, sizeof(value), frameStats.heldBytes);
  char percent[16];
  formatPercent(percent, sizeof(percent), heldFraction);
  std::snprintf(detail, sizeof(detail), "%s", percent);
  drawLivePanel("storage", "Storage", value, detail, {storageW, liveH}, palette, nullptr,
                heldFraction);
  drawStaticInfoStrip(width, palette, capacityBytes, arenaSize, bytesPerBuffer, totalPages,
                      totalBuffers, totalTimeBuffers, totalSignals, messageCount);
}

bool drawMessageRow(const Message& msg, const MessageUiStats& stats, bool expanded, float width,
                    const ArenaPalette& palette) {
  ImGui::PushID(static_cast<int>(msg.id));
  const float height = 64.0f;
  ImGui::InvisibleButton("message", {width, height});
  const bool clicked = ImGui::IsItemClicked();
  const bool hovered = ImGui::IsItemHovered();
  const ImGuiID itemId = ImGui::GetItemID();
  const float focus =
      iam_tween_float(itemId, ImHashStr("focus"),
                      expanded  ? 1.0f
                      : hovered ? 0.55f
                                : 0.0f,
                      0.16f, iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade,
                      ImGui::GetIO().DeltaTime, expanded ? 1.0f : 0.0f);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec4 fill = mixColor(palette.panel, palette.active, focus * 0.62f);
  constexpr float rounding = 8.0f;
  ImGui::RenderFrame(min, max, colorU32(withAlpha(fill, 0.82f)), false, rounding);
  PhotonUi::leftAccentFrame(min, max, colorU32(withAlpha(palette.accent, 0.38f + focus * 0.42f)),
                            rounding, 5.0f);
  draw->AddRect(min, max, colorU32(withAlpha(palette.border, 0.36f + focus * 0.24f)), rounding);

  char idText[32];
  std::snprintf(idText, sizeof(idText), "0x%X", msg.id);
  const float statStart = width > 660.0f ? max.x - 344.0f : max.x;
  draw->PushClipRect({min.x + 16.0f, min.y}, {statStart - 18.0f, max.y}, true);
  draw->AddText({min.x + 16.0f, min.y + 10.0f}, colorU32(palette.text), msg.name.c_str());
  draw->AddText({min.x + 16.0f, min.y + 34.0f}, colorU32(palette.muted), idText);
  draw->PopClipRect();

  if (width > 660.0f) {
    char text[64];
    const float right = max.x - 34.0f;
    formatBytesPerSecond(text, sizeof(text), stats.dataRate);
    draw->AddText({right - 300.0f, min.y + 13.0f}, colorU32(palette.text), text);
    draw->AddText({right - 300.0f, min.y + 36.0f}, colorU32(palette.muted), "rate");

    std::snprintf(text, sizeof(text), "%u", stats.sampleCount);
    draw->AddText({right - 178.0f, min.y + 13.0f}, colorU32(palette.text), text);
    draw->AddText({right - 178.0f, min.y + 36.0f}, colorU32(palette.muted), "samples");

    formatAge(text, sizeof(text),
              stats.lastChangeTime > 0.0 ? ImGui::GetTime() - stats.lastChangeTime : -1.0);
    draw->AddText({right - 72.0f, min.y + 13.0f}, colorU32(palette.text), text);
    draw->AddText({right - 72.0f, min.y + 36.0f}, colorU32(palette.muted), "age");
  }

  const char* chevron = expanded ? "\uea5f" : "\uea61";
  PhotonUi::drawIconCentered(draw, chevron, {max.x - 30.0f, min.y}, {max.x - 10.0f, max.y}, 16.0f,
                             colorU32(mixColor(palette.muted, palette.text, focus)), 1.0f);
  ImGui::PopID();
  return clicked;
}

void Arena::statusUI(int flags) {
  if (ImGui::Begin("Arena Status", nullptr, flags)) {
    static std::array<MessageUiStats, MESSAGE_MAX> uiStats{};
    static UiRing netDataRateHistory{};
    static std::array<bool, MESSAGE_MAX> expandedMessages{};
    static uint64_t cachedArenaGeneration = UINT64_MAX;

    if (cachedArenaGeneration != generation) {
      cachedArenaGeneration = generation;
      for (MessageUiStats& stats : uiStats) stats.reset();
      expandedMessages.fill(false);
      netDataRateHistory.reset();
    }

    const double now = ImGui::GetTime();
    const ArenaUiFrameStats frameStats = refreshArenaUiStats(*this, uiStats);
    netDataRateHistory.push(static_cast<float>(frameStats.netDataRate), now);
    constexpr double bandwidthAverageWindowSeconds = 3.0;
    const double smoothedNetDataRate =
        netDataRateHistory.average(now, bandwidthAverageWindowSeconds);
    const ArenaPalette palette = PhotonUi::palette();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    const size_t capacityBytes = bytesPerBuffer * totalBuffers;
    const float heldFraction = capacityBytes > 0 ? static_cast<float>(frameStats.heldBytes) /
                                                       static_cast<float>(capacityBytes)
                                                 : 0.0f;
    char buf[64];

    drawArenaHeader(contentWidth, palette, frameStats, netDataRateHistory, smoothedNetDataRate,
                    capacityBytes, heldFraction, arenaSize, bytesPerBuffer, totalPages,
                    totalBuffers, totalTimeBuffers, totalSignals, validIds.size());

    ImGui::Spacing();
    PhotonUi::label("Messages", palette);
    static char query[128]{};
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 9.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, withAlpha(palette.panel, 0.82f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, withAlpha(palette.raised, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, withAlpha(palette.raised, 0.96f));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##arena_search", "Search name, id, or signal", query, sizeof(query));
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);

    char normalizedQuery[128];
    const size_t queryLen = normalizeQuery(query, normalizedQuery, sizeof(normalizedQuery));

    ImGui::Spacing();
    uint32_t visibleMessages = 0;
    for (const uint32_t id : validIds) {
      if (id >= messages.size()) continue;
      Message* msg = messages[id];
      if (!msg) continue;
      if (!messageMatchesQuery(*msg, normalizedQuery, queryLen)) continue;
      visibleMessages++;

      MessageUiStats& stats = uiStats[id];
      if (drawMessageRow(*msg, stats, expandedMessages[id], contentWidth, palette))
        expandedMessages[id] = !expandedMessages[id];

      if (expandedMessages[id]) {
        ImGui::PushID(static_cast<int>(msg->id));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(12.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBg, withAlpha(palette.panel, 0.48f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, withAlpha(palette.raised, 0.34f));
        ImGui::PushStyleColor(ImGuiCol_TableBorderLight, withAlpha(palette.border, 0.52f));
        ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, withAlpha(palette.border, 0.62f));
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, withAlpha(palette.raised, 0.72f));

        constexpr ImGuiTableFlags metaFlags =
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
        if (ImGui::BeginTable("message_meta", 6, metaFlags)) {
          ImGui::TableSetupColumn("DLC");
          ImGui::TableSetupColumn("TX");
          ImGui::TableSetupColumn("Transfer");
          ImGui::TableSetupColumn("Bandwidth");
          ImGui::TableSetupColumn("Capacity");
          ImGui::TableSetupColumn("Signals");
          ImGui::TableHeadersRow();
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("%u", msg->dlc);
          ImGui::TableSetColumnIndex(1);
          ImGui::TextUnformatted(msg->transmitter.c_str());
          ImGui::TableSetColumnIndex(2);
          formatBytes(buf, sizeof(buf), static_cast<uint64_t>(stats.heldBytes));
          ImGui::TextUnformatted(buf);
          ImGui::TableSetColumnIndex(3);
          formatPercent(buf, sizeof(buf), stats.bandwidthFraction);
          ImGui::TextUnformatted(buf);
          ImGui::TableSetColumnIndex(4);
          const float fillFraction = bytesPerBuffer > 0 ? static_cast<float>(stats.lastBytes) /
                                                              static_cast<float>(bytesPerBuffer)
                                                        : 0.0f;
          formatPercent(buf, sizeof(buf), fillFraction);
          ImGui::TextUnformatted(buf);
          ImGui::TableSetColumnIndex(5);
          ImGui::Text("%u", msg->signalCount);
          ImGui::EndTable();
        }

        constexpr ImGuiTableFlags signalFlags =
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings |
            ImGuiTableFlags_PadOuterX;
        if (ImGui::BeginTable("signals", 8, signalFlags)) {
          ImGui::TableSetupScrollFreeze(0, 1);
          ImGui::TableSetupColumn("Signal");
          ImGui::TableSetupColumn("Layout");
          ImGui::TableSetupColumn("Type");
          ImGui::TableSetupColumn("Scale");
          ImGui::TableSetupColumn("Offset");
          ImGui::TableSetupColumn("Range");
          ImGui::TableSetupColumn("Unit");
          ImGui::TableSetupColumn("Age");
          ImGui::TableHeadersRow();
          for (size_t s{0uz}; s < msg->signalCount; s++) {
            Signal* sig = msg->signals[s];
            if (!sig) continue;
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(sig->name.c_str());

            ImGui::TableSetColumnIndex(1);
            std::snprintf(buf, sizeof(buf), "%d:%d %s", sig->startBit, sig->length,
                          sig->endianness == 1 ? "le" : "be");
            ImGui::TextUnformatted(buf);

            ImGui::TableSetColumnIndex(2);
            const char* type = sig->type == vFLOAT    ? "float"
                               : sig->type == vDOUBLE ? "double"
                                                      : "int";
            ImGui::Text("%s %s", sig->isSigned ? "s" : "u", type);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", sig->scale);

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.3f", sig->offset);

            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%.3f .. %.3f", sig->min, sig->max);

            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(sig->unit.c_str());

            ImGui::TableSetColumnIndex(7);
            formatAge(buf, sizeof(buf),
                      stats.lastChangeTime > 0.0 ? ImGui::GetTime() - stats.lastChangeTime : -1.0);
            ImGui::TextUnformatted(buf);
          }
          ImGui::EndTable();
        }
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar();
        ImGui::PopID();
      }
      ImGui::Dummy({contentWidth, style.ItemSpacing.y * 0.35f});
    }

    if (visibleMessages == 0) {
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImDrawList* list = ImGui::GetWindowDrawList();
      const float h = 56.0f;
      list->AddRectFilled(pos, {pos.x + contentWidth, pos.y + h},
                          colorU32(withAlpha(palette.panel, 0.72f)), 8.0f);
      list->AddRect(pos, {pos.x + contentWidth, pos.y + h},
                    colorU32(withAlpha(palette.border, 0.44f)), 8.0f);
      list->AddText({pos.x + 14.0f, pos.y + 18.0f}, colorU32(palette.muted), "No messages match");
      ImGui::Dummy({contentWidth, h});
    }
  }
  ImGui::End();
};
