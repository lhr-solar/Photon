#include "plots.hpp"

#include <algorithm>
#include <cstdio>

#include "imgui.h"
#include "implot.h"

bool Plots::signal(Arena& arena, uint32_t id, uint32_t signal, ImVec2 size,
                   const ImPlotSpec& spec) {
  if (id >= arena.messages.size() || !arena.messages[id]) return false;
  Message& msg = *arena.messages[id];
  if (signal >= msg.signalCount || !msg.signals[signal]) return false;

  const uint32_t sampleCount =
      msg.signalSize.value.load(std::memory_order_acquire) / sizeof(double);
  if (!sampleCount) return false;
  const auto* timeValues = static_cast<const double*>(msg.timeData);
  CursorIndex& index = indices[id];
  if (index.generation != arena.generation || index.time != cursor ||
      index.window != windowSeconds || index.count != sampleCount) {
    const double firstTime = cursor - windowSeconds / 2;
    const double lastTime = cursor + windowSeconds / 2;
    const auto* first = std::lower_bound(timeValues, timeValues + sampleCount, firstTime);
    index = {arena.generation,
             cursor,
             windowSeconds,
             sampleCount,
             static_cast<uint32_t>(first - timeValues),
             static_cast<uint32_t>(std::upper_bound(first, timeValues + sampleCount, lastTime) -
                                   timeValues)};
  }

  char name[64];
  std::snprintf(name, sizeof(name), "##%u_%u", id, signal);
  constexpr uint32_t maxPlotSamples = 100;
  const uint32_t samples = index.last - index.first;
  const uint32_t stride = std::max(1u, (samples + maxPlotSamples - 1) / maxPlotSamples);
  const uint32_t visibleCount = (samples + stride - 1) / stride;
  ImPlotSpec plotSpec = spec;
  plotSpec.Stride = stride * sizeof(double);
  ImPlot::SetNextAxisLimits(ImAxis_X1, cursor - windowSeconds / 2, cursor + windowSeconds / 2,
                            ImPlotCond_Always);
  ImPlot::SetNextAxisToFit(ImAxis_Y1);
  if (ImPlot::BeginPlot(name, size)) {
    ImPlot::SetupAxes("time", "value", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
    if (visibleCount)
      ImPlot::PlotLine(msg.signals[signal]->name.c_str(), timeValues + index.first,
                       static_cast<const double*>(msg.signals[signal]->data) + index.first,
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

void Plots::timeline(Arena& arena) {
  double first = 0;
  double last = 0;
  bool found = false;
  for (uint32_t id : arena.validIds) {
    if (id >= arena.messages.size() || !arena.messages[id]) continue;
    Message* msg = arena.messages[id];
    const uint32_t count = msg->signalSize.value.load(std::memory_order_acquire) / sizeof(double);
    if (!count) continue;
    const auto* time = static_cast<const double*>(msg->timeData);
    first = found ? std::min(first, time[0]) : time[0];
    last = found ? std::max(last, time[count - 1]) : time[count - 1];
    found = true;
  }
  if (found && (followLatest || cursor < first || cursor > last)) cursor = last;

  if (ImGui::Begin("Timeline")) {
    if (found) {
      double nextCursor = cursor;
      if (ImGui::SliderScalar("Time", ImGuiDataType_Double, &nextCursor, &first, &last, "%.3f")) {
        cursor = nextCursor;
        followLatest = cursor >= last;
      }
      ImGui::SameLine();
      if (followLatest)
        ImGui::TextUnformatted("Live");
      else if (ImGui::Button("Go Live")) {
        cursor = last;
        followLatest = true;
      }
      const double minWindow = 0.001;
      const double maxWindow = std::max(minWindow, last - first);
      windowSeconds = std::clamp(windowSeconds, minWindow, maxWindow);
      ImGui::SliderScalar("Window", ImGuiDataType_Double, &windowSeconds, &minWindow, &maxWindow,
                          "%.3f s", ImGuiSliderFlags_Logarithmic);
    } else {
      ImGui::TextUnformatted("No samples");
    }
  }
  ImGui::End();
}
