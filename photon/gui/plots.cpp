#include "plots.hpp"

#include <algorithm>
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

void drawTimelineValue(const char* text, ImVec2 size, const PhotonUi::Palette& palette) {
  ImGui::Dummy(size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.82f)),
                      PhotonUi::kFrameRounding);
  draw->AddRect(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.border, 0.42f)),
                PhotonUi::kFrameRounding);
  const ImVec2 textSize = ImGui::CalcTextSize(text);
  draw->PushClipRect(min, max, true);
  draw->AddText({min.x + (size.x - textSize.x) / 2, min.y + (size.y - textSize.y) / 2},
                PhotonUi::colorU32(palette.text), text);
  draw->PopClipRect();
}

void drawLiveIndicator(ImVec2 size, const PhotonUi::Palette& palette) {
  const char* label = "LIVE";
  ImGui::Dummy(size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.active, 0.82f)),
                      PhotonUi::kFrameRounding);
  draw->AddRect(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.accent, 0.42f)),
                PhotonUi::kFrameRounding);
  const ImVec2 textSize = ImGui::CalcTextSize(label);
  const float centerY = (min.y + max.y) / 2;
  const float radius = std::max(2.5f, size.y * 0.11f);
  const float gap = size.y * 0.18f;
  const float contentWidth = radius * 2 + gap + textSize.x;
  const float dotX = min.x + (size.x - contentWidth) / 2 + radius;
  draw->AddCircleFilled({dotX, centerY}, radius, IM_COL32(56, 220, 116, 255));
  draw->AddText({dotX + radius + gap, centerY - textSize.y / 2}, PhotonUi::colorU32(palette.text),
                label);
}

struct TimelineNavigation {
  int value{};
  bool changed{};
  bool committed{};
  bool hidden{};
};

TimelineNavigation drawTimelineNavigator(int level, int value, int limit, float width,
                                         uint8_t& dragging, const PhotonUi::Palette& palette,
                                         const char* clock, bool floating = false,
                                         bool hideable = false) {
  static constexpr int counts[] = {24, 60, 60};
  static constexpr const char* names[] = {"HOUR", "MIN", "SEC"};
  TimelineNavigation result{.value = value};
  const float height = ImGui::GetFrameHeight();
  ImGui::InvisibleButton("##TimelineTime", {width, height});
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
  const float labelWidth = std::min(height * (floating ? 3.35f : 2.35f), width * 0.28f);
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
        draw, "\ueb55", closeMin, closeMax, height * 0.46f,
        PhotonUi::colorU32(PhotonUi::withAlpha(palette.text, closeHovered ? 1.0f : 0.82f)), 1.0f);
  }
  draw->AddText({min.x + closeWidth + 10.0f, min.y + (height - ImGui::GetTextLineHeight()) * 0.5f},
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
      const float textWidth = ImGui::CalcTextSize(label).x;
      draw->AddText({x - textWidth * 0.5f, min.y + 2.0f}, PhotonUi::colorU32(palette.muted), label);
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

  if (held) {
    char notch[8];
    if (level == 0)
      std::snprintf(notch, sizeof(notch), "%d %s", displayHour(result.value),
                    result.value < 12 ? "AM" : "PM");
    else
      std::snprintf(notch, sizeof(notch), "%02d %c", result.value, "HMS"[level]);
    const ImVec2 textSize = ImGui::CalcTextSize(notch);
    const float badgeWidth = textSize.x + 14.0f;
    const float badgeBottom = min.y - 5.0f;
    const float badgeX =
        std::clamp(selectedX - badgeWidth * 0.5f, railMin, std::max(railMin, railMax - badgeWidth));
    const ImVec2 badgeMin{badgeX, badgeBottom - textSize.y - 6.0f};
    const ImVec2 badgeMax{badgeX + badgeWidth, badgeBottom};
    draw->PushClipRectFullScreen();
    draw->AddRectFilled({badgeMin.x + 1.0f, badgeMin.y + 2.0f},
                        {badgeMax.x + 1.0f, badgeMax.y + 2.0f},
                        PhotonUi::colorU32(PhotonUi::withAlpha(palette.bg, 0.55f)), 5.0f);
    draw->AddRectFilled(badgeMin, badgeMax, PhotonUi::colorU32(palette.accent), 5.0f);
    draw->AddTriangleFilled({selectedX - 4.0f, badgeBottom}, {selectedX + 4.0f, badgeBottom},
                            {selectedX, min.y}, PhotonUi::colorU32(palette.accent));
    draw->AddText({badgeX + 7.0f, badgeMin.y + 3.0f}, PhotonUi::colorU32(palette.text), notch);
    draw->PopClipRect();
  }

  if (clockWidth > 0.0f) {
    const ImVec2 textSize = ImGui::CalcTextSize(clock);
    draw->AddText({max.x - clockWidth + (clockWidth - textSize.x) * 0.5f,
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
  if (!localTime(cursor, cursorDate) || !localTime(liveTime, liveDate)) return false;

  constexpr float cell = 30.0f;
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float width = cell * 7 + spacing * 6;
  ImGui::SetNextWindowSize({width + ImGui::GetStyle().WindowPadding.x * 2, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10.0f, 10.0f});
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, PhotonUi::kPopupRounding);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, PhotonUi::withAlpha(palette.bg, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_Border, PhotonUi::withAlpha(palette.border, 0.72f));
  bool changed = false;
  if (ImGui::BeginPopup("TimelineCalendar", ImGuiWindowFlags_AlwaysAutoResize)) {
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

    for (int column = 0; column < 7; ++column) {
      if (column) ImGui::SameLine();
      const float textWidth = ImGui::CalcTextSize(weekdays[column]).x;
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cell - textWidth) * 0.5f);
      ImGui::TextDisabled("%s", weekdays[column]);
      if (column != 6) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cell - textWidth) * 0.5f);
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
    for (int slot = 0; slot < first.tm_wday + days; ++slot) {
      if (slot % 7) ImGui::SameLine();
      const int day = slot - first.tm_wday + 1;
      if (day < 1) {
        ImGui::Dummy({cell, cell});
        continue;
      }
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
    ImPlot::SetupAxes("time", "value", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
    if (visibleCount)
      ImPlot::PlotLine(msg.signals[signal]->name.c_str(), timeValues + firstSample,
                       static_cast<const double*>(msg.signals[signal]->data) + firstSample,
                       static_cast<int>(visibleCount), plotSpec);
    ImPlot::EndPlot();
  }
  return true;
}

bool Plots::signalStatic(Arena& arena, uint32_t id, uint32_t signal, ImVec2 size,
                         const ImPlotSpec& spec) {
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

void Plots::timeline(Arena& arena, Network* network, bool serverConnected, ImVec2 pos,
                     ImVec2 size) {
  // Timeline values are Unix seconds; LIVE advances independently of message cadence.
  const double systemNow =
      std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
  double first = 0;
  double last = 0;
  double locallyAvailableFrom = 0;
  bool found = false;
  for (uint32_t id : arena.validIds) {
    Message* msg = arena.messages[id];
    const uint32_t count = msg->signalSize.value.load(std::memory_order_acquire) / sizeof(double);
    if (!count) continue;
    const auto* time = static_cast<const double*>(msg->timeData);
    first = found ? std::min(first, time[0]) : time[0];
    last = found ? std::max(last, time[count - 1]) : time[count - 1];
    locallyAvailableFrom = found ? std::max(locallyAvailableFrom, time[0]) : time[0];
    found = true;
  }
  if (found) {
    if (followLatest)
      cursor = systemNow;
    else if (!serverConnected)
      cursor = std::clamp(cursor, first, last);
  } else if (followLatest && serverConnected)
    cursor = systemNow;
  if (playing && (serverConnected || (found && first <= cursor && last >= cursor))) {
    cursor = std::min(cursor + ImGui::GetIO().DeltaTime, playTarget);
    if (!serverConnected) cursor = std::min(cursor, last);
    if (cursor >= playTarget) {
      playing = false;
      followLatest = true;
      cursor = systemNow;
      if (serverConnected && network) network->requestTimeline(cursor);
    }
  }
  if (followLatest) timelineLevel = 0;

  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
  const ImGuiStyle& baseStyle = ImGui::GetStyle();
  const float timelineFramePaddingY =
      std::min(baseStyle.FramePadding.y, std::max(2.0f, (size.y - ImGui::GetFontSize()) * 0.5f));
  const float timelineFrameHeight = ImGui::GetFontSize() + timelineFramePaddingY * 2.0f;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {0, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      {baseStyle.FramePadding.x, timelineFramePaddingY});
  ImGui::PushStyleVar(
      ImGuiStyleVar_WindowPadding,
      {baseStyle.WindowPadding.x, std::max(0.0f, (size.y - timelineFrameHeight) / 2)});
  ImVec4 windowBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  windowBg.w = 0.85f;
  ImGui::PushStyleColor(ImGuiCol_WindowBg, windowBg);
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavFocus |
      ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (ImGui::Begin("Timeline", nullptr, flags) && (found || serverConnected)) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const PhotonUi::Palette palette = PhotonUi::palette();
    PhotonUi::Palette goLivePalette = palette;
    const ImVec4 softRed{0.90f, 0.24f, 0.28f, 1.0f};
    goLivePalette.raised = PhotonUi::mixColor(palette.raised, softRed, 0.24f);
    goLivePalette.active = PhotonUi::mixColor(palette.active, softRed, 0.52f);
    goLivePalette.border = PhotonUi::mixColor(palette.border, softRed, 0.48f);
    goLivePalette.muted = PhotonUi::mixColor(palette.muted, softRed, 0.18f);
    const float spacing = style.ItemSpacing.x;
    const float buttonWidth = ImGui::GetFrameHeight();
    const float availableWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x);
    constexpr float minSliderWidth = 96.0f;
    const float fullDateWidth = buttonWidth * 5.25f;
    const float compactDateWidth = buttonWidth * 3.30f;
    const float scaleValueWidth = buttonWidth * 4.25f;
    const float playWidth = buttonWidth * 4.75f;
    const float statusWidth = buttonWidth * 3.5f;
    char scale[32];
    std::snprintf(scale, sizeof(scale), "RANGE %.3f s", windowSeconds);

    bool showDate = false;
    bool compactDate = false;
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

    if (fits(fullDateWidth + buttonWidth * 2.0f + scaleValueWidth + playWidth + statusWidth, 7)) {
      showDate = true;
      showScaleButtons = true;
      showScaleValue = true;
      showPlay = true;
      showStatus = true;
      timeWidth = availableWidth - fullDateWidth - buttonWidth * 2.0f - scaleValueWidth -
                  playWidth - statusWidth - spacing * 6.0f;
    } else if (fits(fullDateWidth + buttonWidth * 2.0f + statusWidth, 5)) {
      showDate = true;
      showScaleButtons = true;
      showStatus = true;
      timeWidth =
          availableWidth - fullDateWidth - buttonWidth * 2.0f - statusWidth - spacing * 4.0f;
    } else if (fits(compactDateWidth + statusWidth, 3)) {
      showDate = true;
      compactDate = true;
      showStatus = true;
      dateWidth = compactDateWidth;
      timeWidth = availableWidth - compactDateWidth - statusWidth - spacing * 2.0f;
    } else if (fits(statusWidth, 2)) {
      showStatus = true;
      timeWidth = availableWidth - statusWidth - spacing;
    }
    timeWidth = std::max(2.0f, timeWidth);

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

    bool hasPreviousItem = false;
    const auto nextTimelineItem = [&]() {
      if (hasPreviousItem)
        ImGui::SameLine(0.0f, spacing);
      else
        hasPreviousItem = true;
    };

    if (showDate) {
      nextTimelineItem();
      if (PhotonUi::rowButton("TimelineDate", "\uea53", compactDate ? compactDateLabel : date,
                              {dateWidth, buttonWidth}, palette)) {
        calendarYear = cursorDate.tm_year + 1900;
        calendarMonth = cursorDate.tm_mon;
        ImGui::OpenPopup("TimelineCalendar");
      }
    }
    const bool today =
        cursorDate.tm_year == liveDate.tm_year && cursorDate.tm_yday == liveDate.tm_yday;
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
        timelineLevel = level;
        cursor = std::min(setTimelinePart(cursor, level, navigation.value), liveTime);
        localTime(cursor, cursorDate);
        formatClock(cursorDate, clock, sizeof(clock));
        followLatest = cursor >= liveTime;
        playing = false;
      }
      if (navigation.committed) {
        timelineLevel = std::min(level + 1, 2);
        if (level == 2 && serverConnected && network &&
            (!found || cursor - windowSeconds < locallyAvailableFrom))
          network->requestTimeline(cursor);
      }
    };

    nextTimelineItem();
    const TimelineNavigation navigation = drawTimelineNavigator(
        0, cursorDate.tm_hour, timelineLimit(0), timeWidth, timelineDragging, palette, clock);
    applyNavigation(0, navigation);

    if (showScaleButtons) {
      nextTimelineItem();
      if (PhotonUi::iconButton("TimelineScaleDown", "\ueaf2", "", {buttonWidth, buttonWidth},
                               palette))
        windowSeconds = std::max(0.001, windowSeconds / 2);
      if (showScaleValue) {
        nextTimelineItem();
        drawTimelineValue(scale, {scaleValueWidth, buttonWidth}, palette);
      }
      nextTimelineItem();
      if (PhotonUi::iconButton("TimelineScaleUp", "\ueb0b", "", {buttonWidth, buttonWidth},
                               palette)) {
        windowSeconds *= 2;
        if (serverConnected && network && (!found || cursor - windowSeconds < locallyAvailableFrom))
          network->requestTimeline(cursor);
      }
    }

    if (showPlay && !followLatest) {
      nextTimelineItem();
      const bool cursorLocal = found && first <= cursor && last >= cursor;
      const bool canPlay =
          playing || (serverConnected ? cursor < liveTime : cursorLocal && cursor < last);
      if (PhotonUi::rowButton("TimelinePlay", playing ? "\ued45" : "\ued46",
                              playing ? "Pause" : "Play from here", {playWidth, buttonWidth},
                              palette, false, !canPlay)) {
        playing = !playing;
        if (playing) {
          playTarget = serverConnected ? liveTime : last;
          if (serverConnected && !cursorLocal && network) network->requestTimeline(cursor);
        }
      }
    }
    if (showStatus) {
      nextTimelineItem();
      if (followLatest) {
        drawLiveIndicator({statusWidth, buttonWidth}, palette);
      } else if (PhotonUi::rowButton("TimelineGoLive", "\ued46", "Go live",
                                     {statusWidth, buttonWidth}, goLivePalette)) {
        cursor = liveTime;
        followLatest = true;
        playing = false;
        if (serverConnected && network) network->requestTimeline(cursor);
      }
    }
    double selectedDate = cursor;
    if (drawCalendarPopup(calendarYear, calendarMonth, cursor, liveTime, selectedDate, palette)) {
      cursor = selectedDate;
      timelineLevel = 0;
      followLatest = cursor >= liveTime;
      playing = false;
      if (serverConnected && network && (!found || cursor - windowSeconds < locallyAvailableFrom))
        network->requestTimeline(cursor);
    }

    constexpr ImGuiWindowFlags floatingFlags = flags | ImGuiWindowFlags_NoFocusOnAppearing |
                                               ImGuiWindowFlags_NoBackground |
                                               ImGuiWindowFlags_Tooltip;
    for (int level = 1; level <= timelineLevel; ++level) {
      char windowName[24];
      std::snprintf(windowName, sizeof(windowName), "TimelinePrecision%d", level);
      ImGui::SetNextWindowPos({pos.x, pos.y - level * (size.y + 5.0f)});
      ImGui::SetNextWindowSize(size);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, PhotonUi::kPopupRounding);
      ImGui::PushStyleVar(
          ImGuiStyleVar_WindowPadding,
          {baseStyle.WindowPadding.x, std::max(0.0f, (size.y - timelineFrameHeight) / 2)});
      TimelineNavigation precision{};
      if (ImGui::Begin(windowName, nullptr, floatingFlags)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        const int value = level == 1 ? cursorDate.tm_min : cursorDate.tm_sec;
        precision = drawTimelineNavigator(level, value, timelineLimit(level),
                                          ImGui::GetContentRegionAvail().x, timelineDragging,
                                          palette, clock, true, level == timelineLevel);
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
