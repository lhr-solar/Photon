#include "plots.hpp"

#include <algorithm>
#include <cstdio>

#include "imgui.h"
#include "implot.h"

bool Plots::signal(Arena& arena, uint32_t id, uint32_t signal, ImVec2 size,
                   const ImPlotSpec& spec) {
  void* data = nullptr;
  void* time = nullptr;
  uint32_t timeBytes = 0;
  uint32_t dataBytes = 0;
  arena.read(id, signal, &data, &dataBytes);
  arena.readTime(id, &time, &timeBytes);
  if (!dataBytes || !timeBytes || id >= arena.messages.size() || !arena.messages[id]) return false;
  if (signal >= arena.messages[id]->signalCount || !arena.messages[id]->signals[signal])
    return false;
  const uint32_t sampleCount = std::min(dataBytes, timeBytes) / sizeof(double);
  const uint32_t visibleCount = std::min(sampleCount, 100u);
  if (visibleCount == 0) return false;
  const uint32_t firstSample = sampleCount - visibleCount;
  char name[64]{};
  std::snprintf(name, sizeof(name), "##%u_%u", id, signal);
  if (ImPlot::BeginPlot(name, size)) {
    ImPlot::SetupAxes("time", "value", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
    ImPlot::PlotLine(arena.messages[id]->signals[signal]->name.c_str(),
                     static_cast<const double*>(time) + firstSample,
                     static_cast<const double*>(data) + firstSample, static_cast<int>(visibleCount),
                     spec);
    ImPlot::EndPlot();
  }
  return true;
}
