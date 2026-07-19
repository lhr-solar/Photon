#include "plots.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "imgui.h"
#include "implot.h"
#include "uiComponents.hpp"

namespace {
void formatTimestamp(double seconds, char* text, size_t size) {
  int64_t totalMilliseconds = static_cast<int64_t>(std::floor(seconds * 1000.0));
  std::time_t wholeSeconds = static_cast<std::time_t>(totalMilliseconds / 1000);
  int milliseconds = static_cast<int>(totalMilliseconds % 1000);
  if (milliseconds < 0) {
    milliseconds += 1000;
    --wholeSeconds;
  }
  std::tm local{};
#ifdef _WIN32
  const bool valid = localtime_s(&local, &wholeSeconds) == 0;
#else
  const bool valid = localtime_r(&wholeSeconds, &local) != nullptr;
#endif
  if (!valid) {
    std::snprintf(text, size, "%.3f", seconds);
    return;
  }
  std::snprintf(text, size, "%04d-%02d-%02d  %02d:%02d:%02d.%03d", local.tm_year + 1900,
                local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec,
                milliseconds);
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

bool drawTimelineSlider(double& value, double first, double last, float width,
                        const PhotonUi::Palette& palette) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, PhotonUi::kFrameRounding);
  ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, PhotonUi::kFrameRounding);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, PhotonUi::withAlpha(palette.panel, 0.82f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, PhotonUi::withAlpha(palette.raised, 0.92f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, PhotonUi::withAlpha(palette.active, 0.92f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, PhotonUi::withAlpha(palette.accent, 0.70f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, palette.accent);
  ImGui::SetNextItemWidth(width);
  const bool changed =
      ImGui::SliderScalar("##TimelineTime", ImGuiDataType_Double, &value, &first, &last, "");
  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(2);

  char timestamp[48]{};
  formatTimestamp(value, timestamp, sizeof(timestamp));
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const float iconWidth = max.y - min.y;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  PhotonUi::drawIconCentered(draw, "\uea70", min, {min.x + iconWidth, max.y}, 16.0f,
                             PhotonUi::colorU32(palette.muted), 1.0f);
  const ImVec2 textSize = ImGui::CalcTextSize(timestamp);
  draw->PushClipRect({min.x + iconWidth, min.y}, max, true);
  draw->AddText({min.x + iconWidth + std::max(0.0f, (width - iconWidth - textSize.x) / 2),
                 min.y + (max.y - min.y - textSize.y) / 2},
                PhotonUi::colorU32(palette.text), timestamp);
  draw->PopClipRect();
  const char* clock = std::strrchr(timestamp, ' ');
  PhotonUi::tooltip(clock ? clock + 1 : timestamp);
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

void Plots::timeline(Arena& arena, ImVec2 pos, ImVec2 size) {
  double first = 0;
  double last = 0;
  bool found = false;
  for (uint32_t id : arena.validIds) {
    Message* msg = arena.messages[id];
    const uint32_t count = msg->signalSize.value.load(std::memory_order_acquire) / sizeof(double);
    if (!count) continue;
    const auto* time = static_cast<const double*>(msg->timeData);
    first = found ? std::min(first, time[0]) : time[0];
    last = found ? std::max(last, time[count - 1]) : time[count - 1];
    found = true;
  }
  if (found && (followLatest || cursor < first || cursor > last)) cursor = last;

  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {0, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(
      ImGuiStyleVar_WindowPadding,
      {ImGui::GetStyle().WindowPadding.x, std::max(0.0f, (size.y - ImGui::GetFrameHeight()) / 2)});
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavFocus |
      ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (ImGui::Begin("Timeline", nullptr, flags) && found) {
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
    const float scaleValueWidth = buttonWidth * 4.25f;
    const float statusWidth = buttonWidth * 3.5f;
    char scale[32];
    std::snprintf(scale, sizeof(scale), "SCALE %.3f s", windowSeconds);
    const float timeWidth = std::max(2.0f, ImGui::GetContentRegionAvail().x - buttonWidth * 2 -
                                               scaleValueWidth - statusWidth - spacing * 4);
    double nextCursor = cursor;
    if (drawTimelineSlider(nextCursor, first, last, timeWidth, palette)) {
      cursor = nextCursor;
      followLatest = cursor >= last;
    }
    ImGui::SameLine();
    if (PhotonUi::iconButton("TimelineScaleDown", "\ueaf2", "", {buttonWidth, buttonWidth},
                             palette))
      windowSeconds = std::max(0.001, windowSeconds / 2);
    ImGui::SameLine();
    drawTimelineValue(scale, {scaleValueWidth, buttonWidth}, palette);
    ImGui::SameLine();
    if (PhotonUi::iconButton("TimelineScaleUp", "\ueb0b", "", {buttonWidth, buttonWidth}, palette))
      windowSeconds *= 2;
    ImGui::SameLine();
    if (followLatest) {
      drawLiveIndicator({statusWidth, buttonWidth}, palette);
    } else if (PhotonUi::rowButton("TimelineGoLive", "\ued46", "Go live",
                                   {statusWidth, buttonWidth}, goLivePalette)) {
      cursor = last;
      followLatest = true;
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(4);
}
