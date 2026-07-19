#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "../parse/arena.hpp"
#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "plots.hpp"
#include "uiComponents.hpp"

using ArenaPalette = PhotonUi::Palette;
using PhotonUi::colorU32;
using PhotonUi::mixColor;
using PhotonUi::withAlpha;

constexpr float kArenaTableRowHeight = 38.0f;
constexpr double kArenaDisplaySmoothingSeconds = 0.65;

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

bool formatLatestSignalValue(char* out, size_t outSize, const Signal& signal,
                             uint32_t publishedBytes) {
  if (!signal.data || publishedBytes < sizeof(double)) {
    std::snprintf(out, outSize, "no data");
    return false;
  }

  double value = 0.0;
  const auto* data = static_cast<const uint8_t*>(signal.data);
  std::memcpy(&value, data + publishedBytes - sizeof(double), sizeof(value));

  const double magnitude = std::fabs(value);
  if (!std::isfinite(value)) {
    std::snprintf(out, outSize, "%g", value);
  } else if ((magnitude > 0.0 && magnitude < 0.001) || magnitude >= 100000.0) {
    std::snprintf(out, outSize, "%.4g", value);
  } else {
    std::snprintf(out, outSize, "%.3f", value);
  }
  return true;
}

inline double smoothDisplayValue(double current, double target, double elapsed,
                                 double smoothingSeconds = kArenaDisplaySmoothingSeconds) {
  if (elapsed <= 0.0 || smoothingSeconds <= 0.0) return target;
  const double alpha = 1.0 - std::exp(-elapsed / smoothingSeconds);
  return current + (target - current) * std::clamp(alpha, 0.0, 1.0);
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
  double displayHeldBytes{};
  double displayDataRate{};
  double displayBandwidthFraction{};
  double displayCapacityFraction{};
  double displayElapsed{};
  double lastPollTime{};
  double lastChangeTime{};
  double dataRate{};
  double bandwidthFraction{};
  double capacityFraction{};

  void reset() {
    initialized = false;
    lastBytes = 0;
    sampleCount = 0;
    heldBytes = 0;
    displayHeldBytes = 0.0;
    displayDataRate = 0.0;
    displayBandwidthFraction = 0.0;
    displayCapacityFraction = 0.0;
    displayElapsed = 0.0;
    lastPollTime = 0.0;
    lastChangeTime = 0.0;
    dataRate = 0.0;
    bandwidthFraction = 0.0;
    capacityFraction = 0.0;
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
    stats.capacityFraction =
        arena.bytesPerBuffer > 0
            ? static_cast<double>(signalBytes) / static_cast<double>(arena.bytesPerBuffer)
            : 0.0;

    if (!stats.initialized) {
      stats.initialized = true;
      stats.lastBytes = signalBytes;
      stats.lastPollTime = now;
      stats.lastChangeTime = signalBytes > 0 ? now : 0.0;
      stats.dataRate = 0.0;
      stats.displayElapsed = 0.0;
      stats.displayHeldBytes = static_cast<double>(stats.heldBytes);
      stats.displayDataRate = 0.0;
      stats.displayCapacityFraction = stats.capacityFraction;
    } else {
      const uint32_t deltaBytes =
          signalBytes >= stats.lastBytes ? signalBytes - stats.lastBytes : signalBytes;
      const double elapsed = now - stats.lastPollTime;
      stats.displayElapsed = elapsed;
      const size_t deltaHeldBytes =
          static_cast<size_t>(deltaBytes) * (static_cast<size_t>(msg.signalCount) + 1);
      stats.dataRate = elapsed > 0.0 ? static_cast<double>(deltaHeldBytes) / elapsed : 0.0;
      if (deltaBytes > 0 || signalBytes < stats.lastBytes) stats.lastChangeTime = now;
      stats.lastBytes = signalBytes;
      stats.lastPollTime = now;
      stats.displayHeldBytes =
          smoothDisplayValue(stats.displayHeldBytes, static_cast<double>(stats.heldBytes), elapsed);
      stats.displayDataRate = smoothDisplayValue(stats.displayDataRate, stats.dataRate, elapsed);
      stats.displayCapacityFraction =
          smoothDisplayValue(stats.displayCapacityFraction, stats.capacityFraction, elapsed);
    }

    frame.heldBytes += stats.heldBytes;
    frame.netDataRate += stats.dataRate;
  }

  for (const uint32_t id : arena.validIds) {
    if (id >= arena.messages.size() || !arena.messages[id]) continue;

    MessageUiStats& stats = cache[id];
    stats.bandwidthFraction = frame.netDataRate > 0.0 ? stats.dataRate / frame.netDataRate : 0.0;
    stats.displayBandwidthFraction = smoothDisplayValue(
        stats.displayBandwidthFraction, stats.bandwidthFraction, stats.displayElapsed);
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

void drawClippedText(ImDrawList* draw, ImVec2 pos, ImVec2 clipMin, ImVec2 clipMax, ImU32 color,
                     std::string_view text) {
  draw->PushClipRect(clipMin, clipMax, true);
  draw->AddText(pos, color, text.data(), text.data() + text.size());
  draw->PopClipRect();
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
    formatBytesPerSecond(text, sizeof(text), stats.displayDataRate);
    drawClippedText(draw, {right - 300.0f, min.y + 13.0f}, {right - 300.0f, min.y},
                    {right - 202.0f, max.y}, colorU32(palette.text), text);
    drawClippedText(draw, {right - 300.0f, min.y + 36.0f}, {right - 300.0f, min.y},
                    {right - 202.0f, max.y}, colorU32(palette.muted), "rate");

    std::snprintf(text, sizeof(text), "%u", stats.sampleCount);
    drawClippedText(draw, {right - 178.0f, min.y + 13.0f}, {right - 178.0f, min.y},
                    {right - 96.0f, max.y}, colorU32(palette.text), text);
    drawClippedText(draw, {right - 178.0f, min.y + 36.0f}, {right - 178.0f, min.y},
                    {right - 96.0f, max.y}, colorU32(palette.muted), "samples");

    formatAge(text, sizeof(text),
              stats.lastChangeTime > 0.0 ? ImGui::GetTime() - stats.lastChangeTime : -1.0);
    drawClippedText(draw, {right - 72.0f, min.y + 13.0f}, {right - 72.0f, min.y},
                    {right - 4.0f, max.y}, colorU32(palette.text), text);
    drawClippedText(draw, {right - 72.0f, min.y + 36.0f}, {right - 72.0f, min.y},
                    {right - 4.0f, max.y}, colorU32(palette.muted), "age");
  }

  const char* chevron = expanded ? "\uea5f" : "\uea61";
  PhotonUi::drawIconCentered(draw, chevron, {max.x - 30.0f, min.y}, {max.x - 10.0f, max.y}, 16.0f,
                             colorU32(mixColor(palette.muted, palette.text, focus)), 1.0f);
  ImGui::PopID();
  return clicked;
}

void tableCell(int column, std::string_view text, const ArenaPalette& palette, bool muted = false) {
  ImGui::TableSetColumnIndex(column);
  ImGui::Dummy({0.0f, kArenaTableRowHeight - ImGui::GetStyle().CellPadding.y * 2.0f});
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImRect clipRect = ImGui::TableGetCellBgRect(ImGui::GetCurrentTable(), column);
  drawClippedText(ImGui::GetWindowDrawList(), min, clipRect.Min, clipRect.Max,
                  colorU32(muted ? palette.muted : palette.text), text);
}

void tableCellf(int column, const ArenaPalette& palette, const char* fmt, ...) {
  char text[96];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  tableCell(column, text, palette);
}

bool tableRowClicked(int columnCount, const ArenaPalette& palette) {
  const ImGuiTable* table = ImGui::GetCurrentTable();
  if (!table || columnCount <= 0) return false;

  const ImRect first = ImGui::TableGetCellBgRect(table, 0);
  const ImRect last = ImGui::TableGetCellBgRect(table, columnCount - 1);
  const ImRect rowRect(first.Min, {last.Max.x, first.Max.y});
  const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                       rowRect.Contains(ImGui::GetIO().MousePos);
  if (hovered) {
    ImGui::GetWindowDrawList()->AddRectFilled(rowRect.Min, rowRect.Max,
                                              colorU32(withAlpha(palette.active, 0.12f)));
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }
  return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

struct ArenaTableColumn {
  const char* label;
  float weight;
};

void setupArenaTableColumns(std::initializer_list<ArenaTableColumn> columns) {
  constexpr ImGuiTableColumnFlags columnFlags =
      ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize |
      ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide |
      ImGuiTableColumnFlags_NoHeaderWidth;
  for (const ArenaTableColumn& column : columns)
    ImGui::TableSetupColumn(column.label, columnFlags, column.weight);
  ImGui::TableNextRow(ImGuiTableRowFlags_Headers, kArenaTableRowHeight);
  int columnIndex = 0;
  for (const ArenaTableColumn& column : columns) {
    ImGui::TableSetColumnIndex(columnIndex++);
    ImGui::TableHeader(column.label);
  }
}

float arenaTableHeight(size_t rows) { return kArenaTableRowHeight * static_cast<float>(rows + 1); }

float arenaTablePanelHeight(size_t rows) {
  const ImGuiStyle& style = ImGui::GetStyle();
  return style.WindowPadding.y * 2.0f + ImGui::GetTextLineHeight() + style.ItemSpacing.y +
         arenaTableHeight(rows);
}

bool beginArenaTablePanel(const char* id, std::string_view label, float width, float height,
                          const ArenaPalette& palette) {
  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::Dummy({width, style.ItemSpacing.y * 0.25f});
  const bool open =
      PhotonUi::beginPanel(id, {width, height}, palette, ImGuiChildFlags_Borders,
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
  if (open) {
    const ImVec2 min = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        min, {min.x + ImGui::GetWindowWidth(), min.y + ImGui::GetTextLineHeight() + 18.0f},
        colorU32(withAlpha(palette.raised, 0.30f)), style.ChildRounding);
    PhotonUi::label(label, palette);
  }
  return open;
}

void endArenaTablePanel() { PhotonUi::endPanel(); }

void drawSignalPlotPopup(Arena& arena, uint32_t id, uint32_t signal, const ArenaPalette& palette) {
  if (id >= arena.messages.size() || !arena.messages[id]) return;
  Message* msg = arena.messages[id];
  if (signal >= msg->signalCount || !msg->signals[signal]) return;

  const bool open = PhotonUi::beginModal("Signal Plot", {760.0f, 440.0f});
  if (!open) return;

  char heading[192];
  std::snprintf(heading, sizeof(heading), "%s / %s", msg->name.c_str(),
                msg->signals[signal]->name.c_str());
  PhotonUi::label(heading, palette);
  ImGui::Spacing();

  const ImVec2 plotSize{-FLT_MIN, std::max(260.0f, ImGui::GetContentRegionAvail().y - 48.0f)};
  ImPlotSpec spec{ImPlotProp_LineWeight, 4.0f};
  if (!Plots::signalStatic(arena, id, signal, plotSize, spec)) {
    ImGui::Dummy(plotSize);
    const ImVec2 min = ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddText({min.x + 14.0f, min.y + 14.0f}, colorU32(palette.muted),
                                        "No samples yet");
  }

  if (PhotonUi::modalCloseButton("CloseSignalPlot", palette)) ImGui::CloseCurrentPopup();
  PhotonUi::endModal(open);
}

void Arena::statusUI(int flags) {
  PhotonUi::pushContentStyle();
  if (ImGui::Begin("Arena Status", nullptr, flags)) {
    static std::array<MessageUiStats, MESSAGE_MAX> uiStats{};
    static UiRing netDataRateHistory{};
    static std::array<bool, MESSAGE_MAX> expandedMessages{};
    static uint32_t plotMessageId = 0;
    static uint32_t plotSignalIndex = 0;
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
    PhotonUi::pushInputStyle(palette);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##arena_search", "Search name, id, or signal", query, sizeof(query));
    PhotonUi::popInputStyle();

    char normalizedQuery[128];
    const size_t queryLen = normalizeQuery(query, normalizedQuery, sizeof(normalizedQuery));

    ImGui::Spacing();
    bool signalPlotRequested = false;
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
        PhotonUi::pushTableStyle(palette);
        constexpr ImGuiTableFlags tableFlags =
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings |
            ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoHostExtendY;

        if (beginArenaTablePanel("##message_meta_panel", "Message", contentWidth,
                                 arenaTablePanelHeight(1), palette)) {
          if (ImGui::BeginTable("message_meta", 6, tableFlags, {0.0f, arenaTableHeight(1)})) {
            setupArenaTableColumns({{"DLC", 0.60f},
                                    {"TX", 1.35f},
                                    {"Transfer", 1.10f},
                                    {"Bandwidth", 1.00f},
                                    {"Capacity", 0.90f},
                                    {"Signals", 0.70f}});
            ImGui::TableNextRow(ImGuiTableRowFlags_None, kArenaTableRowHeight);
            tableCellf(0, palette, "%u", msg->dlc);
            tableCell(1, msg->transmitter, palette);
            formatBytes(buf, sizeof(buf), static_cast<uint64_t>(stats.displayHeldBytes));
            tableCell(2, buf, palette);
            formatPercent(buf, sizeof(buf), stats.displayBandwidthFraction);
            tableCell(3, buf, palette);
            formatPercent(buf, sizeof(buf), stats.displayCapacityFraction);
            tableCell(4, buf, palette);
            tableCellf(5, palette, "%u", msg->signalCount);
            ImGui::EndTable();
          }
        }
        endArenaTablePanel();

        if (beginArenaTablePanel("##signals_panel", "Signals", contentWidth,
                                 arenaTablePanelHeight(msg->signalCount), palette)) {
          if (ImGui::BeginTable("signals", 9, tableFlags,
                                {0.0f, arenaTableHeight(msg->signalCount)})) {
            setupArenaTableColumns({{"Signal", 1.60f},
                                    {"Layout", 0.92f},
                                    {"Type", 0.72f},
                                    {"Scale", 0.78f},
                                    {"Offset", 0.78f},
                                    {"Range", 1.18f},
                                    {"Unit", 0.68f},
                                    {"Value", 0.92f},
                                    {"Age", 0.72f}});
            for (size_t s{0uz}; s < msg->signalCount; s++) {
              Signal* sig = msg->signals[s];
              if (!sig) continue;
              ImGui::TableNextRow(ImGuiTableRowFlags_None, kArenaTableRowHeight);

              tableCell(0, sig->name, palette);

              std::snprintf(buf, sizeof(buf), "%d:%d %s", sig->startBit, sig->length,
                            sig->endianness == 1 ? "le" : "be");
              tableCell(1, buf, palette, true);

              const char* type = sig->type == vFLOAT    ? "float"
                                 : sig->type == vDOUBLE ? "double"
                                                        : "int";
              tableCellf(2, palette, "%s %s", sig->isSigned ? "s" : "u", type);

              tableCellf(3, palette, "%.3f", sig->scale);

              tableCellf(4, palette, "%.3f", sig->offset);

              tableCellf(5, palette, "%.3f .. %.3f", sig->min, sig->max);

              tableCell(6, sig->unit, palette, sig->unit.empty());

              const bool hasValue =
                  formatLatestSignalValue(buf, sizeof(buf), *sig, stats.lastBytes);
              tableCell(7, buf, palette, !hasValue);

              formatAge(
                  buf, sizeof(buf),
                  stats.lastChangeTime > 0.0 ? ImGui::GetTime() - stats.lastChangeTime : -1.0);
              tableCell(8, buf, palette, true);

              if (tableRowClicked(9, palette)) {
                plotMessageId = msg->id;
                plotSignalIndex = static_cast<uint32_t>(s);
                signalPlotRequested = true;
              }
            }
            ImGui::EndTable();
          }
        }
        endArenaTablePanel();
        PhotonUi::popTableStyle();
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
    if (signalPlotRequested) ImGui::OpenPopup("Signal Plot");
    drawSignalPlotPopup(*this, plotMessageId, plotSignalIndex, palette);
  }
  ImGui::End();
  PhotonUi::popContentStyle();
};
