#include "plots.hpp"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "../network/network.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include "uiComponents.hpp"

namespace {
bool localTime(double seconds, std::tm& local) {
  const std::time_t wholeSeconds = static_cast<std::time_t>(std::floor(seconds));
#ifdef _WIN32
  return localtime_s(&local, &wholeSeconds) == 0;
#else
  return localtime_r(&wholeSeconds, &local) != nullptr;
#endif
}

int displayHour(int hour) {
  hour %= 12;
  return hour ? hour : 12;
}

void formatClock(const std::tm& local, char* text, size_t size) {
  std::snprintf(text, size, "%d:%02d:%02d %s", displayHour(local.tm_hour), local.tm_min,
                local.tm_sec, local.tm_hour < 12 ? "AM" : "PM");
}

void formatTimestamp(double seconds, char* text, size_t size) {
  int64_t totalMilliseconds = static_cast<int64_t>(std::floor(seconds * 1000.0));
  std::time_t wholeSeconds = static_cast<std::time_t>(totalMilliseconds / 1000);
  int milliseconds = static_cast<int>(totalMilliseconds % 1000);
  if (milliseconds < 0) {
    milliseconds += 1000;
    --wholeSeconds;
  }
  std::tm local{};
  if (!localTime(static_cast<double>(wholeSeconds), local)) {
    std::snprintf(text, size, "%.3f", seconds);
    return;
  }
  std::snprintf(text, size, "%04d-%02d-%02d  %d:%02d:%02d.%03d %s", local.tm_year + 1900,
                local.tm_mon + 1, local.tm_mday, displayHour(local.tm_hour), local.tm_min,
                local.tm_sec, milliseconds, local.tm_hour < 12 ? "AM" : "PM");
}

void drawTimelineValue(const char* text, ImVec2 size, const PhotonUi::Palette& palette,
                       float fontSize) {
  ImGui::Dummy(size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.82f)),
                      PhotonUi::kFrameRounding);
  draw->AddRect(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.border, 0.42f)),
                PhotonUi::kFrameRounding);
  ImFont* font = ImGui::GetFont();
  const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
  draw->PushClipRect(min, max, true);
  draw->AddText(font, fontSize,
                {min.x + (size.x - textSize.x) / 2, min.y + (size.y - textSize.y) / 2},
                PhotonUi::colorU32(palette.text), text);
  draw->PopClipRect();
}

void drawLiveIndicator(ImVec2 size, const PhotonUi::Palette& palette, float fontSize) {
  const char* label = "LIVE";
  ImGui::Dummy(size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.active, 0.82f)),
                      PhotonUi::kFrameRounding);
  draw->AddRect(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.accent, 0.42f)),
                PhotonUi::kFrameRounding);
  ImFont* font = ImGui::GetFont();
  const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
  const float centerY = (min.y + max.y) / 2;
  const float radius = std::max(2.5f, size.y * 0.11f);
  const float gap = size.y * 0.18f;
  const float contentWidth = radius * 2 + gap + textSize.x;
  const float dotX = min.x + (size.x - contentWidth) / 2 + radius;
  draw->AddCircleFilled({dotX, centerY}, radius, IM_COL32(56, 220, 116, 255));
  draw->AddText(font, fontSize, {dotX + radius + gap, centerY - textSize.y / 2},
                PhotonUi::colorU32(palette.text), label);
}

struct TimelineNavigation {
  int value{};
  bool changed{};
  bool committed{};
  bool hidden{};
};

enum class TimelineDateMode : uint8_t { Hidden, Icon, Compact, Full };

TimelineNavigation drawTimelineNavigator(int level, int value, int limit, ImVec2 size,
                                         uint8_t& dragging, const PhotonUi::Palette& palette,
                                         const char* clock, float fontSize, float iconSize,
                                         bool floating = false, bool hideable = false) {
  static constexpr int counts[] = {24, 60, 60};
  static constexpr const char* names[] = {"HOUR", "MIN", "SEC"};
  TimelineNavigation result{.value = value};
  const float width = size.x;
  const float height = size.y;
  ImFont* font = ImGui::GetFont();
  ImGui::InvisibleButton("##TimelineTime", size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec4 fill = hovered || active ? palette.raised : palette.panel;
  draw->AddRectFilled(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(fill, 0.88f)),
                      PhotonUi::kFrameRounding);
  draw->AddRect(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.border, 0.42f)),
                PhotonUi::kFrameRounding);

  const float closeWidth = floating ? height : 0.0f;
  const float labelWidth = std::min(height * 2.35f, width * 0.28f);
  float clockWidth = width >= 190.0f ? std::min(height * 3.0f, width * 0.30f) : 0.0f;
  float railMin = min.x + labelWidth + 6.0f;
  float railMax = max.x - clockWidth - 10.0f;
  if (railMax < railMin + 24.0f) {
    clockWidth = 0.0f;
    railMin = min.x + std::min(height, width * 0.25f);
    railMax = max.x - 6.0f;
  }

  const float mouseX = ImGui::GetIO().MousePos.x;
  const uint8_t dragBit = 1u << level;
  const bool closeHovered = hideable && hovered && mouseX < min.x + closeWidth;
  if (ImGui::IsItemActivated()) {
    if (closeHovered) {
      result.hidden = true;
      dragging = 0;
    } else if (mouseX >= railMin && mouseX <= railMax) {
      dragging |= dragBit;
    } else {
      dragging &= ~dragBit;
    }
  }
  if (active && (dragging & dragBit) && railMax > railMin) {
    const float normalized = std::clamp((mouseX - railMin) / (railMax - railMin), 0.0f, 1.0f);
    result.value = std::min(static_cast<int>(std::round(normalized * (counts[level] - 1))), limit);
    result.changed = true;
  }
  if (ImGui::IsItemDeactivated()) {
    result.committed = dragging & dragBit;
    dragging &= ~dragBit;
  }
  const bool held = active && (dragging & dragBit);

  if (hideable) {
    const ImVec4 red{0.90f, 0.24f, 0.28f, 1.0f};
    const ImVec2 closeMin{min.x + 3.0f, min.y + 3.0f};
    const ImVec2 closeMax{min.x + closeWidth - 3.0f, max.y - 3.0f};
    draw->AddRectFilled(
        closeMin, closeMax,
        PhotonUi::colorU32(closeHovered ? PhotonUi::mixColor(palette.raised, red, 0.28f)
                                        : PhotonUi::withAlpha(palette.raised, 0.76f)),
        PhotonUi::kFrameRounding);
    draw->AddRect(closeMin, closeMax,
                  PhotonUi::colorU32(closeHovered ? PhotonUi::mixColor(palette.border, red, 0.62f)
                                                  : palette.border),
                  PhotonUi::kFrameRounding);
    PhotonUi::drawIconCentered(
        draw, "\ueb55", closeMin, closeMax, iconSize,
        PhotonUi::colorU32(PhotonUi::withAlpha(palette.text, closeHovered ? 1.0f : 0.82f)), 1.0f);
  }
  const ImVec2 nameSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, names[level]);
  draw->AddText(font, fontSize, {min.x + closeWidth + 10.0f, min.y + (height - nameSize.y) * 0.5f},
                PhotonUi::colorU32(palette.muted), names[level]);

  const float baseline = max.y - 6.0f;
  draw->AddLine({railMin, baseline}, {railMax, baseline},
                PhotonUi::colorU32(PhotonUi::withAlpha(palette.border, 0.70f)), 1.0f);
  const int count = counts[level];
  const float railWidth = railMax - railMin;
  const int majorStep = level == 0 ? (railWidth >= 320.0f ? 3 : 6)
                                   : (railWidth >= 520.0f   ? 5
                                      : railWidth >= 300.0f ? 10
                                                            : 15);
  for (int tick = 0; tick < count; ++tick) {
    const float x = railMin + railWidth * tick / (count - 1);
    const bool selected = tick == result.value;
    const bool available = tick <= limit;
    const bool major = tick % majorStep == 0;
    const float tickHeight = selected ? 13.0f : major ? 8.0f : 4.0f;
    const ImVec4 color = selected ? palette.accent : available ? palette.muted : palette.panel;
    draw->AddLine({x, baseline - tickHeight}, {x, baseline}, PhotonUi::colorU32(color),
                  selected ? 2.0f : 1.0f);
    if (major && railWidth >= 150.0f) {
      char label[5];
      if (level == 0)
        std::snprintf(label, sizeof(label), "%d%s", displayHour(tick), tick < 12 ? "AM" : "PM");
      else
        std::snprintf(label, sizeof(label), "%02d", tick);
      const float textWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label).x;
      draw->AddText(font, fontSize, {x - textWidth * 0.5f, min.y + 2.0f},
                    PhotonUi::colorU32(palette.muted), label);
    }
  }
  const float selectedX = railMin + railWidth * result.value / (count - 1);
  const ImVec2 handle{selectedX, baseline};
  if (held) {
    draw->AddCircleFilled(handle, 7.0f,
                          PhotonUi::colorU32(PhotonUi::withAlpha(palette.accent, 0.22f)));
    draw->AddCircle(handle, 7.0f, PhotonUi::colorU32(palette.accent), 0, 1.5f);
  }
  draw->AddCircleFilled(handle, held ? 4.0f : 3.5f, PhotonUi::colorU32(palette.accent));
  draw->AddCircle(handle, held ? 4.0f : 3.5f, PhotonUi::colorU32(palette.text), 0, 1.0f);

  if (clockWidth > 0.0f) {
    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, clock);
    draw->AddText(font, fontSize,
                  {max.x - clockWidth + (clockWidth - textSize.x) * 0.5f,
                   min.y + (height - textSize.y) * 0.5f},
                  PhotonUi::colorU32(palette.text), clock);
  }
  return result;
}

double setTimelinePart(double cursor, int level, int value) {
  std::tm date{};
  if (!localTime(cursor, date)) return cursor;
  if (level == 0) {
    date.tm_hour = value;
    date.tm_min = date.tm_sec = 0;
  } else if (level == 1) {
    date.tm_min = value;
    date.tm_sec = 0;
  } else {
    date.tm_sec = value;
  }
  date.tm_isdst = -1;
  return static_cast<double>(std::mktime(&date));
}

bool drawCalendarPopup(int& year, int& month, double cursor, double liveTime, double& selected,
                       const PhotonUi::Palette& palette) {
  static constexpr const char* months[] = {"January",   "February", "March",    "April",
                                           "May",       "June",     "July",     "August",
                                           "September", "October",  "November", "December"};
  static constexpr const char* weekdays[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
  std::tm cursorDate{}, liveDate{};
  const bool timesOk = localTime(cursor, cursorDate) && localTime(liveTime, liveDate);

  constexpr float cell = 30.0f;
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  constexpr float padding = 10.0f;
  const float width = cell * 7 + spacing * 6;
  ImGui::SetNextWindowSize({width + padding * 2.0f, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {padding, padding});
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, PhotonUi::kPopupRounding);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, PhotonUi::withAlpha(palette.bg, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_Border, PhotonUi::withAlpha(palette.border, 0.72f));
  bool changed = false;
  // Always BeginPopup when open — skipping it abandons the OpenPopup request.
  if (ImGui::BeginPopup("TimelineCalendar", ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!timesOk) {
      ImGui::EndPopup();
      ImGui::PopStyleColor(2);
      ImGui::PopStyleVar(2);
      return false;
    }
    constexpr float calendarFontSize = 14.0f;
    const float previousFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
    const float windowIndependentFontSize =
        ImGui::GetFontSize() / std::max(previousFontScale, 0.001f);
    ImGui::SetWindowFontScale(calendarFontSize / std::max(windowIndependentFontSize, 1.0f));
    PhotonUi::pushInputStyle(palette);
    ImGui::SetNextItemWidth(width - 88.0f - spacing);
    if (ImGui::BeginCombo("##CalendarMonth", months[month])) {
      for (int value = 0; value < 12; ++value) {
        const bool future = year == liveDate.tm_year + 1900 && value > liveDate.tm_mon;
        ImGui::BeginDisabled(future);
        if (ImGui::Selectable(months[value], month == value)) month = value;
        ImGui::EndDisabled();
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(88.0f);
    char selectedYear[5];
    std::snprintf(selectedYear, sizeof(selectedYear), "%d", year);
    if (ImGui::BeginCombo("##CalendarYear", selectedYear)) {
      for (int value = liveDate.tm_year + 1900; value >= 1970; --value) {
        char label[5];
        std::snprintf(label, sizeof(label), "%d", value);
        if (ImGui::Selectable(label, year == value)) year = value;
      }
      ImGui::EndCombo();
    }
    PhotonUi::popInputStyle();
    if (year == liveDate.tm_year + 1900) month = std::min(month, liveDate.tm_mon);

    const ImVec2 weekdayStart = ImGui::GetCursorPos();
    const float weekdayHeight = ImGui::GetTextLineHeight();
    for (int column = 0; column < 7; ++column) {
      const float textWidth = ImGui::CalcTextSize(weekdays[column]).x;
      ImGui::SetCursorPos(
          {weekdayStart.x + column * (cell + spacing) + (cell - textWidth) * 0.5f, weekdayStart.y});
      ImGui::TextDisabled("%s", weekdays[column]);
    }

    std::tm first{};
    first.tm_year = year - 1900;
    first.tm_mon = month;
    first.tm_mday = 1;
    first.tm_isdst = -1;
    std::mktime(&first);
    static constexpr int monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int days = monthDays[month] + (month == 1 && leap);
    const ImVec2 dayStart{weekdayStart.x, weekdayStart.y + weekdayHeight + spacing};
    for (int slot = 0; slot < first.tm_wday + days; ++slot) {
      const int day = slot - first.tm_wday + 1;
      if (day < 1) continue;
      const int column = slot % 7;
      const int row = slot / 7;
      ImGui::SetCursorPos(
          {dayStart.x + column * (cell + spacing), dayStart.y + row * (cell + spacing)});
      const bool future =
          year == liveDate.tm_year + 1900 && month == liveDate.tm_mon && day > liveDate.tm_mday;
      char label[3];
      std::snprintf(label, sizeof(label), "%d", day);
      ImGui::BeginDisabled(future);
      ImGui::PushID(day);
      if (PhotonUi::button("Day", label, {cell, cell}, palette,
                           year == cursorDate.tm_year + 1900 && month == cursorDate.tm_mon &&
                               day == cursorDate.tm_mday)) {
        std::tm date = cursorDate;
        date.tm_year = year - 1900;
        date.tm_mon = month;
        date.tm_mday = day;
        date.tm_isdst = -1;
        selected = std::min(static_cast<double>(std::mktime(&date)), liveTime);
        changed = true;
        ImGui::CloseCurrentPopup();
      }
      ImGui::PopID();
      ImGui::EndDisabled();
    }
    const int rows = (first.tm_wday + days + 6) / 7;
    ImGui::SetCursorPos({dayStart.x, dayStart.y + rows * cell + std::max(0, rows - 1) * spacing});
    ImGui::SetWindowFontScale(previousFontScale);
    ImGui::EndPopup();
  }
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
  return changed;
}
}  // namespace

bool Plots::signal(Arena& arena, uint32_t id, uint32_t signal, ImVec2 size,
                   const ImPlotSpec& spec) {
  if (id >= arena.messages.size() || !arena.messages[id]) return false;
  Message& msg = *arena.messages[id];
  if (signal >= msg.signalCount || !msg.signals[signal]) return false;

  const uint32_t sampleCount =
      msg.signalSize.value.load(std::memory_order_acquire) / sizeof(double);
  if (!sampleCount) return false;
  const auto* timeValues = static_cast<const double*>(msg.timeData);
  if (index.id != id || index.generation != arena.generation || index.time != cursor ||
      index.window != windowSeconds || index.count != sampleCount) {
    const auto* first =
        std::lower_bound(timeValues, timeValues + sampleCount, cursor - windowSeconds);
    index = {arena.generation,
             cursor,
             windowSeconds,
             id,
             sampleCount,
             static_cast<uint32_t>(first - timeValues),
             static_cast<uint32_t>(std::upper_bound(first, timeValues + sampleCount, cursor) -
                                   timeValues)};
  }

  char name[64];
  std::snprintf(name, sizeof(name), "##%u_%u", id, signal);
  const uint32_t maxPlotSamples = static_cast<uint32_t>(std::max(size.x, 100.0f));
  const uint32_t samples = index.last - index.first;
  const uint32_t stride = std::max(1u, (samples + maxPlotSamples - 1) / maxPlotSamples);
  const uint32_t firstSample = index.first + (stride - index.first % stride) % stride;
  const uint32_t visibleCount = (index.last - firstSample + stride - 1) / stride;
  ImPlotSpec plotSpec = spec;
  plotSpec.Stride = stride * sizeof(double);
  ImPlot::SetNextAxisLimits(ImAxis_X1, cursor - windowSeconds, cursor, ImPlotCond_Always);
  ImPlot::SetNextAxisToFit(ImAxis_Y1);
  if (ImPlot::BeginPlot(name, size)) {
    const char* yAxis = nullptr;
    if (msg.signals[signal] && !msg.signals[signal]->unit.empty() &&
        msg.signals[signal]->unit != "NULL")
      yAxis = msg.signals[signal]->unit.c_str();
    ImPlot::SetupAxes("time", yAxis, ImPlotAxisFlags_None, ImPlotAxisFlags_None);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
    if (visibleCount) {
      std::string series = msg.signals[signal]->name;
      if (yAxis) series.append(" (").append(yAxis).append(")");
      ImPlot::PlotLine(series.c_str(), timeValues + firstSample,
                       static_cast<const double*>(msg.signals[signal]->data) + firstSample,
                       static_cast<int>(visibleCount), plotSpec);
    }
    ImPlot::EndPlot();
  }
  return true;
}

bool Plots::signalStatic(Arena& arena, uint32_t id, uint32_t signal, ImVec2 size,
                         const ImPlotSpec& spec) {
  ArenaReadScope read(arena);
  void* data = nullptr;
  void* time = nullptr;
  uint32_t timeBytes = 0;
  uint32_t dataBytes = 0;
  arena.read(id, signal, &data, &dataBytes);
  arena.readTime(id, &time, &timeBytes);
  if (!dataBytes || !timeBytes) return false;
  if (id >= arena.messages.size() || !arena.messages[id]) return false;
  if (signal >= arena.messages[id]->signalCount || !arena.messages[id]->signals[signal])
    return false;

  char name[64];
  std::snprintf(name, sizeof(name), "##%u_%u", id, signal);
  const char* signalName = arena.messages[id]->signals[signal]->name.c_str();
  constexpr uint32_t maxPlotSamples = 100;
  const uint32_t sampleCount = std::min(dataBytes, timeBytes) / sizeof(double);
  const uint32_t visibleCount = std::min(sampleCount, maxPlotSamples);
  if (visibleCount == 0) return false;
  uint32_t firstSample = sampleCount - visibleCount;
  const auto* timeValues = static_cast<const double*>(time) + firstSample;
  const auto* dataValues = static_cast<const double*>(data) + firstSample;
  ImPlot::SetNextAxisToFit(ImAxis_X1);
  ImPlot::SetNextAxisToFit(ImAxis_Y1);
  if (ImPlot::BeginPlot(name, size)) {
    ImPlot::SetupAxes("time", "value", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
    ImPlot::PlotLine(signalName, timeValues, dataValues, static_cast<int>(visibleCount), spec);
    ImPlot::EndPlot();
  }
  return true;
}

double Plots::mapCursor() const {
  if (timelineMode != TimelineMode::Live) return cursor;
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void Plots::scrubTo(double timeSeconds, double visibleSeconds, Network* network) {
  if (!std::isfinite(timeSeconds)) return;
  cursor = timeSeconds;
  if (std::isfinite(visibleSeconds) && visibleSeconds > 0.0)
    windowSeconds = std::clamp(visibleSeconds, 0.001, PlotManager::kMaxTimeWindowSeconds);

  const bool leaveLive = timelineMode == TimelineMode::Live ||
                         timelineMode == TimelineMode::Playing ||
                         timelineMode == TimelineMode::Buffering;
  timelineMode = TimelineMode::Paused;
  timelineLevel = 0;
  if (leaveLive && network) network->requestTimeline(CANP_TIMELINE_PAUSE);
}

void Plots::setViewWindowSeconds(double visibleSeconds) {
  if (!std::isfinite(visibleSeconds) || visibleSeconds <= 0.0) return;
  windowSeconds = std::clamp(visibleSeconds, 0.001, PlotManager::kMaxTimeWindowSeconds);
}

void Plots::goLive(Network* network) {
  cursor =
      std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
  timelineMode = TimelineMode::Live;
  timelineLevel = 0;
  if (network) network->requestTimeline(CANP_TIMELINE_LIVE);
}

void Plots::timeline(Arena& arena, Network* network, bool serverConnected, ImVec2 pos,
                     ImVec2 size) {
  // Timeline values are Unix seconds; LIVE advances independently of message cadence.
  const double systemNow =
      std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
  double first = 0;
  double last = 0;
  bool found = false;
  {
    ArenaReadScope read(arena);
    for (uint32_t id : arena.validIds) {
      if (id >= arena.messages.size()) continue;
      Message* msg = arena.messages[id];
      if (!msg || !msg->timeData) continue;
      const uint32_t count = msg->signalSize.value.load(std::memory_order_acquire) / sizeof(double);
      if (!count) continue;
      const auto* time = static_cast<const double*>(msg->timeData);
      first = found ? std::min(first, time[0]) : time[0];
      last = found ? std::max(last, time[count - 1]) : time[count - 1];
      found = true;
    }
  }
  if (timelineMode == TimelineMode::Live) cursor = systemNow;
  if (timelineMode == TimelineMode::Buffering) {
    if (!serverConnected) {
      timelineMode = TimelineMode::Paused;
    } else if (network && network->timelineCursor.statusSequence.load(std::memory_order_acquire) !=
                              playbackStatusSequence) {
      const uint64_t response = network->timelineCursor.response.load(std::memory_order_relaxed);
      const uint16_t status = TimelineCursorMailbox::command(response);
      if (status == CANP_TIMELINE_UNAVAILABLE) {
        timelineMode = TimelineMode::Unavailable;
      } else if (status == CANP_TIMELINE_PLAY) {
        const uint64_t latest =
            network->timelineCursor.latestTimestampMs.load(std::memory_order_acquire);
        if (cursor >= playTarget) {
          timelineMode = TimelineMode::Live;
          cursor = systemNow;
          network->requestTimeline(CANP_TIMELINE_LIVE);
        } else if (latest >= static_cast<uint64_t>(std::min(cursor + 2.0, playTarget) * 1000.0) ||
                   (latest >= static_cast<uint64_t>(cursor * 1000.0) &&
                    ImGui::GetTime() - bufferingSince >= 0.25)) {
          timelineMode = TimelineMode::Playing;
        }
      }
    }
  }
  if (timelineMode == TimelineMode::Playing &&
      (serverConnected || (found && first <= cursor && last >= cursor))) {
    cursor = std::min(cursor + ImGui::GetIO().DeltaTime, playTarget);
    if (!serverConnected) cursor = std::min(cursor, last);
    if (cursor >= playTarget) {
      timelineMode = TimelineMode::Live;
      cursor = systemNow;
      if (serverConnected && network) network->requestTimeline(CANP_TIMELINE_LIVE);
    }
  }
  if (timelineMode == TimelineMode::Live) timelineLevel = 0;

  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
  const ImGuiStyle& baseStyle = ImGui::GetStyle();
  const float controlHeight = std::min(size.y, 30.0f);
  const float timelineFontSize = controlHeight * 0.42f;
  const float timelineIconSize = controlHeight * 0.47f;
  const float timelineFramePaddingY = std::max(0.0f, (controlHeight - ImGui::GetFontSize()) * 0.5f);
  const float timelineWindowPaddingY = std::max(0.0f, (size.y - controlHeight) * 0.5f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {0, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      {baseStyle.FramePadding.x, timelineFramePaddingY});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, timelineWindowPaddingY});
  ImVec4 windowBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  windowBg.w = 0.85f;
  ImGui::PushStyleColor(ImGuiCol_WindowBg, windowBg);
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavFocus |
      ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (ImGui::Begin("Timeline", nullptr, flags) && (found || serverConnected)) {
    const PhotonUi::Palette palette = PhotonUi::palette();
    PhotonUi::Palette goLivePalette = palette;
    const ImVec4 softRed{0.90f, 0.24f, 0.28f, 1.0f};
    goLivePalette.raised = PhotonUi::mixColor(palette.raised, softRed, 0.24f);
    goLivePalette.active = PhotonUi::mixColor(palette.active, softRed, 0.52f);
    goLivePalette.border = PhotonUi::mixColor(palette.border, softRed, 0.48f);
    goLivePalette.muted = PhotonUi::mixColor(palette.muted, softRed, 0.18f);
    constexpr float spacing = 6.0f;
    constexpr float maxTimelineWidth = 640.0f;
    const float buttonWidth = controlHeight;
    const float availableWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x);
    constexpr float minSliderWidth = 96.0f;
    const double liveTime = systemNow;
    std::tm cursorDate{}, liveDate{};
    localTime(cursor, cursorDate);
    localTime(liveTime, liveDate);
    char date[48];
    formatTimestamp(cursor, date, sizeof(date));
    date[10] = '\0';
    char clock[16];
    formatClock(cursorDate, clock, sizeof(clock));
    char compactDateLabel[16];
    std::snprintf(compactDateLabel, sizeof(compactDateLabel), "%02d-%02d", cursorDate.tm_mon + 1,
                  cursorDate.tm_mday);

    ImFont* font = ImGui::GetFont();
    const auto timelineRowButtonWidth = [&](const char* label) {
      const float labelWidth = font->CalcTextSizeA(timelineFontSize, FLT_MAX, 0.0f, label).x;
      return std::ceil(buttonWidth + labelWidth + 24.0f);
    };
    const float fullDateWidth = timelineRowButtonWidth(date);
    const float compactDateWidth = timelineRowButtonWidth(compactDateLabel);
    const float iconDateWidth = buttonWidth;
    const float scaleValueWidth = buttonWidth * 4.25f;
    const float playWidth = buttonWidth * 4.75f;
    const float statusWidth = buttonWidth * 3.5f;
    const float activePlayWidth = timelineMode == TimelineMode::Live ? 0.0f : playWidth;
    char scale[32];
    std::snprintf(scale, sizeof(scale), "RANGE %.3f s", windowSeconds);

    TimelineDateMode dateMode = TimelineDateMode::Hidden;
    bool showScaleButtons = false;
    bool showScaleValue = false;
    bool showPlay = false;
    bool showStatus = false;
    float dateWidth = fullDateWidth;
    float timeWidth = std::max(2.0f, availableWidth);

    const auto fits = [&](float nonSliderWidth, int visibleItems) {
      const float spacingWidth = spacing * static_cast<float>(std::max(0, visibleItems - 1));
      return availableWidth >= nonSliderWidth + minSliderWidth + spacingWidth;
    };
    const auto showTimelineDate = [&](TimelineDateMode mode, float width) {
      dateMode = mode;
      dateWidth = width;
    };

    if (fits(fullDateWidth + buttonWidth * 2.0f + scaleValueWidth + activePlayWidth + statusWidth,
             activePlayWidth > 0.0f ? 7 : 6)) {
      showTimelineDate(TimelineDateMode::Full, fullDateWidth);
      showScaleButtons = true;
      showScaleValue = true;
      showPlay = activePlayWidth > 0.0f;
      showStatus = true;
      timeWidth = availableWidth - fullDateWidth - buttonWidth * 2.0f - scaleValueWidth -
                  activePlayWidth - statusWidth - spacing * (activePlayWidth > 0.0f ? 6.0f : 5.0f);
    } else if (fits(compactDateWidth + buttonWidth * 2.0f + scaleValueWidth + activePlayWidth +
                        statusWidth,
                    activePlayWidth > 0.0f ? 7 : 6)) {
      showTimelineDate(TimelineDateMode::Compact, compactDateWidth);
      showScaleButtons = true;
      showScaleValue = true;
      showPlay = activePlayWidth > 0.0f;
      showStatus = true;
      timeWidth = availableWidth - compactDateWidth - buttonWidth * 2.0f - scaleValueWidth -
                  activePlayWidth - statusWidth - spacing * (activePlayWidth > 0.0f ? 6.0f : 5.0f);
    } else if (fits(fullDateWidth + buttonWidth * 2.0f + statusWidth, 5)) {
      showTimelineDate(TimelineDateMode::Full, fullDateWidth);
      showScaleButtons = true;
      showStatus = true;
      timeWidth =
          availableWidth - fullDateWidth - buttonWidth * 2.0f - statusWidth - spacing * 4.0f;
    } else if (fits(compactDateWidth + buttonWidth * 2.0f + statusWidth, 5)) {
      showTimelineDate(TimelineDateMode::Compact, compactDateWidth);
      showScaleButtons = true;
      showStatus = true;
      timeWidth =
          availableWidth - compactDateWidth - buttonWidth * 2.0f - statusWidth - spacing * 4.0f;
    } else if (fits(compactDateWidth + statusWidth, 3)) {
      showTimelineDate(TimelineDateMode::Compact, compactDateWidth);
      timeWidth = availableWidth - compactDateWidth - statusWidth - spacing * 2.0f;
      showStatus = true;
    } else if (fits(iconDateWidth + statusWidth, 3)) {
      showTimelineDate(TimelineDateMode::Icon, iconDateWidth);
      timeWidth = availableWidth - iconDateWidth - statusWidth - spacing * 2.0f;
      showStatus = true;
    } else if (fits(statusWidth, 2)) {
      showStatus = true;
      timeWidth = availableWidth - statusWidth - spacing;
    }
    const float reservedWidth = availableWidth - timeWidth;
    timeWidth = std::clamp(timeWidth, 2.0f, maxTimelineWidth);
    const float groupWidth = reservedWidth + timeWidth;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.0f, (availableWidth - groupWidth) * 0.5f));

    bool hasPreviousItem = false;
    const auto nextTimelineItem = [&]() {
      if (hasPreviousItem)
        ImGui::SameLine(0.0f, spacing);
      else
        hasPreviousItem = true;
    };

    if (dateMode != TimelineDateMode::Hidden) {
      nextTimelineItem();
      // Draw the date control, then open on mouse *release*. Opening on IsItemClicked
      // (mouse down) lets the following release over Timeline immediately close the popup.
      if (dateMode == TimelineDateMode::Icon)
        PhotonUi::iconButton("TimelineDate", "\uea53", date, {dateWidth, controlHeight}, palette,
                             false, timelineIconSize);
      else
        PhotonUi::rowButton("TimelineDate", "\uea53",
                            dateMode == TimelineDateMode::Compact ? compactDateLabel : date,
                            {dateWidth, controlHeight}, palette, false, false, false,
                            timelineFontSize, timelineIconSize);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
          ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        calendarYear = cursorDate.tm_year + 1900;
        calendarMonth = cursorDate.tm_mon;
      }
      ImGui::OpenPopupOnItemClick("TimelineCalendar", ImGuiPopupFlags_MouseButtonLeft);
    }
    const bool today =
        cursorDate.tm_year == liveDate.tm_year && cursorDate.tm_yday == liveDate.tm_yday;
    const auto pauseTimeline = [&]() {
      if (timelineMode == TimelineMode::Paused) return;
      timelineMode = TimelineMode::Paused;
      if (serverConnected && network) network->requestTimeline(CANP_TIMELINE_PAUSE);
    };
    const auto startPlayback = [&]() {
      timelineMode = TimelineMode::Paused;
      playTarget = serverConnected ? liveTime : last;
      if (serverConnected && network) {
        if (cursor >= playTarget) {
          timelineMode = TimelineMode::Live;
          network->requestTimeline(CANP_TIMELINE_LIVE);
          return;
        }
        playbackStatusSequence =
            network->timelineCursor.statusSequence.load(std::memory_order_acquire);
        bufferingSince = ImGui::GetTime();
        timelineMode = TimelineMode::Buffering;
        network->requestTimeline(CANP_TIMELINE_PLAY, cursor);
      } else if (found && first <= cursor && cursor < last) {
        timelineMode = TimelineMode::Playing;
      }
    };
    const auto timelineLimit = [&](int level) {
      if (!today) return level == 0 ? 23 : 59;
      if (level == 0) return liveDate.tm_hour;
      if (level == 1 && cursorDate.tm_hour == liveDate.tm_hour) return liveDate.tm_min;
      if (level == 2 && cursorDate.tm_hour == liveDate.tm_hour &&
          cursorDate.tm_min == liveDate.tm_min)
        return liveDate.tm_sec;
      return 59;
    };
    const auto applyNavigation = [&](int level, const TimelineNavigation& navigation) {
      if (navigation.hidden) {
        timelineLevel = timelineDragging = 0;
        return;
      }
      if (navigation.changed) {
        pauseTimeline();
        timelineLevel = level;
        cursor = std::min(setTimelinePart(cursor, level, navigation.value), liveTime);
        localTime(cursor, cursorDate);
        formatClock(cursorDate, clock, sizeof(clock));
      }
      if (navigation.committed) {
        timelineLevel = std::min(level + 1, 2);
        startPlayback();
      }
    };

    nextTimelineItem();
    const TimelineNavigation navigation =
        drawTimelineNavigator(0, cursorDate.tm_hour, timelineLimit(0), {timeWidth, controlHeight},
                              timelineDragging, palette, clock, timelineFontSize, timelineIconSize);
    const ImVec2 hourBarMin = ImGui::GetItemRectMin();
    const float hourBarWidth = ImGui::GetItemRectSize().x;
    applyNavigation(0, navigation);

    if (showScaleButtons) {
      nextTimelineItem();
      if (PhotonUi::iconButton("TimelineScaleDown", "\ueaf2", "", {buttonWidth, buttonWidth},
                               palette, false, timelineIconSize))
        windowSeconds = std::max(0.001, windowSeconds / 2);
      if (showScaleValue) {
        nextTimelineItem();
        drawTimelineValue(scale, {scaleValueWidth, controlHeight}, palette, timelineFontSize);
      }
      nextTimelineItem();
      if (PhotonUi::iconButton("TimelineScaleUp", "\ueb0b", "", {buttonWidth, buttonWidth}, palette,
                               false, timelineIconSize)) {
        windowSeconds *= 2;
      }
    }

    if (showPlay && timelineMode != TimelineMode::Live) {
      nextTimelineItem();
      const bool cursorLocal = found && first <= cursor && last >= cursor;
      const bool activePlayback =
          timelineMode == TimelineMode::Buffering || timelineMode == TimelineMode::Playing;
      const bool canPlay =
          activePlayback || (serverConnected ? cursor < liveTime : cursorLocal && cursor < last);
      const char* playLabel = timelineMode == TimelineMode::Buffering     ? "Buffering"
                              : timelineMode == TimelineMode::Unavailable ? "Retry"
                              : activePlayback                            ? "Pause"
                                                                          : "Play from here";
      if (PhotonUi::rowButton("TimelinePlay", activePlayback ? "\ued45" : "\ued46", playLabel,
                              {playWidth, buttonWidth}, palette, false, !canPlay, false,
                              timelineFontSize, timelineIconSize)) {
        if (activePlayback) {
          pauseTimeline();
        } else {
          startPlayback();
        }
      }
    }
    if (showStatus) {
      nextTimelineItem();
      if (timelineMode == TimelineMode::Live) {
        drawLiveIndicator({statusWidth, controlHeight}, palette, timelineFontSize);
      } else if (PhotonUi::rowButton("TimelineGoLive", "\ued46", "Go live",
                                     {statusWidth, controlHeight}, goLivePalette, false, false,
                                     false, timelineFontSize, timelineIconSize)) {
        cursor = liveTime;
        timelineMode = TimelineMode::Live;
        if (serverConnected && network) network->requestTimeline(CANP_TIMELINE_LIVE);
      }
    }
    double selectedDate = cursor;
    if (drawCalendarPopup(calendarYear, calendarMonth, cursor, liveTime, selectedDate, palette)) {
      cursor = selectedDate;
      timelineLevel = 0;
      startPlayback();
    }

    constexpr ImGuiWindowFlags floatingFlags = flags | ImGuiWindowFlags_NoFocusOnAppearing |
                                               ImGuiWindowFlags_NoBackground |
                                               ImGuiWindowFlags_Tooltip;
    for (int level = 1; level <= timelineLevel; ++level) {
      char windowName[24];
      std::snprintf(windowName, sizeof(windowName), "TimelinePrecision%d", level);
      ImGui::SetNextWindowPos({hourBarMin.x, pos.y - level * (size.y + 5.0f)});
      ImGui::SetNextWindowSize({hourBarWidth, size.y});
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, PhotonUi::kPopupRounding);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, timelineWindowPaddingY});
      TimelineNavigation precision{};
      if (ImGui::Begin(windowName, nullptr, floatingFlags)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        const int value = level == 1 ? cursorDate.tm_min : cursorDate.tm_sec;
        precision = drawTimelineNavigator(
            level, value, timelineLimit(level), {hourBarWidth, controlHeight}, timelineDragging,
            palette, clock, timelineFontSize, timelineIconSize, true, level == timelineLevel);
      }
      ImGui::End();
      ImGui::PopStyleVar(2);
      applyNavigation(level, precision);
    }
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(5);
}
