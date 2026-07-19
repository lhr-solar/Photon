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
  const uint32_t maxFirstSample = sampleCount - visibleCount;
  const uint32_t firstSample =
      cursor > 0 ? std::min(static_cast<uint32_t>(cursor), maxFirstSample) : maxFirstSample;
  char name[64]{};
  std::snprintf(name, sizeof(name), "##%u_%u", id, signal);
  ImPlot::SetNextAxisToFit(ImAxis_X1);
  ImPlot::SetNextAxisToFit(ImAxis_Y1);
  if (ImPlot::BeginPlot(name, size)) {
    ImPlot::SetupAxes("time", "value", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
    ImPlot::PlotLine(arena.messages[id]->signals[signal]->name.c_str(),
                     static_cast<const double*>(time) + firstSample,
                     static_cast<const double*>(data) + firstSample, static_cast<int>(visibleCount),
                     spec);
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

// this simply indexes into the existing signal data array
// fyi, it can actually overflow
// ideally, we have it control a specific time and date
void Plots::timeline(){
    if(ImGui::Begin("timeline ui", NULL, 0)){
        ImGui::Text("test output");
        ImGui::DragInt("controller", &cursor);
    }; ImGui::End();
};
