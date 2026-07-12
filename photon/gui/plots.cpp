#include "plots.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "../parse/arena.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"

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

namespace {
enum PlotTypeIndex : int {
  PlotType_Line = 0,
  PlotType_FilledLine,
  PlotType_Shaded,
  PlotType_Scatter,
  PlotType_Stairstep,
  PlotType_Bar,
  PlotType_BarGroups,
  PlotType_BarStacks,
  PlotType_ErrorBars,
  PlotType_Stem,
  PlotType_Pie,
  PlotType_Heatmap,
  PlotType_Histogram,
  PlotType_Histogram2D,
  PlotType_Digital,
  PlotType_3DLine,
  PlotType_3DScatter,
  PlotType_3DSurface,
  PlotType_List,
  PlotType_Count
};

constexpr std::array<PlotManager::PlotTypeSpec, PlotType_Count> kPlotSpecs{{
    {"Line Plots", 1, 8, true, false},       {"Filled Line Plots", 1, 8, true, false},
    {"Shaded Plots", 2, 2, true, false},     {"Scatter Plots", 1, 8, true, false},
    {"Stairstep Plots", 1, 8, true, false},  {"Bar Plots", 1, 8, true, false},
    {"Bar Groups", 2, 8, true, false},       {"Bar Stacks", 2, 8, true, false},
    {"Error Bars", 2, 2, true, false},       {"Stem Plots", 1, 8, true, false},
    {"Pie Charts", 1, 8, false, false},      {"Heatmaps", 1, 1, false, false},
    {"Histogram", 1, 8, false, false},       {"Histogram 2D", 2, 2, false, false},
    {"Digital Plots", 1, 8, true, false},    {"3D Line Plots", 2, 3, false, true},
    {"3D Scatter Plots", 2, 3, false, true}, {"3D Surface Plots", 2, 3, false, true},
    {"List", 1, 128, false, false},
}};

constexpr std::array<const char*, PlotType_Count> kPlotTypeKeys{{
    "line",    "filled-line", "shaded",     "scatter",      "stairstep",
    "bar",     "bar-groups",  "bar-stacks", "error-bars",   "stem",
    "pie",     "heatmap",     "histogram",  "histogram-2d", "digital",
    "line-3d", "scatter-3d",  "surface-3d", "list",
}};

constexpr size_t kMaxRenderablePoints = 4096;
constexpr size_t kMaxRenderableScatterPoints = 512;
constexpr size_t kMaxRenderableHeatmapCells = 1024;
constexpr size_t kMaxRenderable3DLinePoints = 1024;
constexpr size_t kMaxRenderable3DScatterPoints = 256;
constexpr int kMaxSurfaceSide = 24;

struct RenderSlice {
  size_t start = 0;
  size_t step = 1;
  int count = 0;
};

RenderSlice makeRenderSlice(size_t start, size_t end, size_t maxPoints = kMaxRenderablePoints) {
  RenderSlice slice{};
  slice.start = start;
  if (end <= start) return slice;
  const size_t total = end - start;
  slice.step = std::max<size_t>(1, (total + maxPoints - 1) / maxPoints);
  slice.count = static_cast<int>((total + slice.step - 1) / slice.step);
  return slice;
}

int required3DSources(bool useSource1TimeAsX) { return useSource1TimeAsX ? 2 : 3; }

const char* threeDSourceLabel(size_t sourceIndex, bool useSource1TimeAsX) {
  if (useSource1TimeAsX) return sourceIndex == 0 ? "Y Source" : "Z Source";
  if (sourceIndex == 0) return "X Source";
  if (sourceIndex == 1) return "Y Source";
  return "Z Source";
}

std::string sourceSlotLabel(int typeIndex, size_t sourceIndex, bool useSource1TimeAsX) {
  if (kPlotSpecs[static_cast<size_t>(typeIndex)].is3D)
    return threeDSourceLabel(sourceIndex, useSource1TimeAsX);
  char label[32]{};
  std::snprintf(label, sizeof(label), "Signal %zu", sourceIndex + 1);
  return label;
}

Message* findMessage(Arena* arena, uint32_t messageId) {
  if (!arena || messageId >= arena->messages.size()) return nullptr;
  return arena->messages[messageId];
}

Signal* findSignal(Arena* arena, const PlotManager::PlotSourceRef& ref) {
  if (!ref.assigned) return nullptr;
  Message* msg = findMessage(arena, ref.messageId);
  if (!msg) return nullptr;
  if (!ref.signalName.empty()) {
    for (uint32_t index = 0; index < msg->signalCount; ++index) {
      Signal* signal = msg->signals[index];
      if (signal && signal->name == ref.signalName) return signal;
    }
    return nullptr;
  }
  if (ref.signalIndex >= msg->signalCount) return nullptr;
  return msg->signals[ref.signalIndex];
}

uint32_t resolvedSignalIndex(Arena* arena, const PlotManager::PlotSourceRef& ref) {
  Message* msg = findMessage(arena, ref.messageId);
  if (!msg) return SIGNAL_MAX;
  if (!ref.signalName.empty()) {
    for (uint32_t index = 0; index < msg->signalCount; ++index) {
      Signal* signal = msg->signals[index];
      if (signal && signal->name == ref.signalName) return index;
    }
    return SIGNAL_MAX;
  }
  return ref.signalIndex < msg->signalCount ? ref.signalIndex : SIGNAL_MAX;
}

bool readSource(Arena* arena, const PlotManager::PlotSourceRef& ref, const double*& times,
                const double*& values, int& count) {
  times = nullptr;
  values = nullptr;
  count = 0;
  if (!ref.assigned || !arena) return false;

  void* timeData = nullptr;
  void* signalData = nullptr;
  uint32_t timeBytes = 0;
  uint32_t signalBytes = 0;
  const uint32_t signalIndex = resolvedSignalIndex(arena, ref);
  if (signalIndex == SIGNAL_MAX) return false;
  arena->readTime(ref.messageId, &timeData, &timeBytes);
  arena->read(ref.messageId, signalIndex, &signalData, &signalBytes);
  const uint32_t bytes = std::min(timeBytes, signalBytes);
  const int samples = static_cast<int>(bytes / sizeof(double));
  if (samples <= 0 || !timeData || !signalData) return false;
  times = static_cast<const double*>(timeData);
  values = static_cast<const double*>(signalData);
  count = samples;
  return true;
}

bool readFirstLiveSource(Arena* arena, const std::vector<PlotManager::PlotSourceRef>& sources,
                         const double*& times, const double*& values, int& count) {
  for (const auto& source : sources) {
    if (readSource(arena, source, times, values, count) && count >= 2) return true;
  }
  times = nullptr;
  values = nullptr;
  count = 0;
  return false;
}

double paddedMin(double minValue, double maxValue) {
  if (std::abs(maxValue - minValue) < 1e-9) {
    const double span = std::max(1.0, std::abs(minValue));
    return minValue - span * 0.5;
  }
  return minValue - ((maxValue - minValue) * 0.1);
}

double paddedMax(double minValue, double maxValue) {
  if (std::abs(maxValue - minValue) < 1e-9) {
    const double span = std::max(1.0, std::abs(maxValue));
    return maxValue + span * 0.5;
  }
  return maxValue + ((maxValue - minValue) * 0.1);
}

bool hasAllSources(const std::vector<PlotManager::PlotSourceRef>& sources) {
  if (sources.empty()) return false;
  for (const auto& source : sources)
    if (!source.assigned) return false;
  return true;
}

bool sharesMessageClock(const std::vector<PlotManager::PlotSourceRef>& sources) {
  if (sources.empty()) return false;
  const uint32_t messageId = sources.front().messageId;
  return std::all_of(sources.begin(), sources.end(), [messageId](const auto& source) {
    return source.assigned && source.messageId == messageId;
  });
}

std::string formatTimeWindow(double seconds) {
  char label[32]{};
  if (seconds < 1.0)
    std::snprintf(label, sizeof(label), "%.1f s", seconds);
  else if (seconds < 60.0)
    std::snprintf(label, sizeof(label), "%.0f s", seconds);
  else if (seconds < 3600.0)
    std::snprintf(label, sizeof(label), "%.1f min", seconds / 60.0);
  else
    std::snprintf(label, sizeof(label), "%.1f h", seconds / 3600.0);
  return label;
}

void drawTimeWindowStatus(const PlotManager::PlotWindow& plot) {
  const std::string label = plot.followLatest ? "AUTO  " + formatTimeWindow(plot.timeWindowSeconds)
                                               : "MANUAL";
  const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
  const float rightAlignedX =
      ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - textSize.x);
  ImGui::SetCursorPosX(rightAlignedX);
  ImGui::TextDisabled("%s", label.c_str());
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Scroll over the plot to change the autofit window (0.1 s to 24 h)");
}

void updateFollowState(PlotManager::PlotWindow& plot) {
  const ImGuiIO& io = ImGui::GetIO();
  const bool isDragging =
      ImPlot::IsPlotHovered() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
                                  ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                                  ImGui::IsMouseDragging(ImGuiMouseButton_Middle));
  const bool isScrolling =
      ImPlot::IsPlotHovered() && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f);
  const bool recenterToLive =
      ImPlot::IsPlotHovered() && io.KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

  const ImPlotRect limits = ImPlot::GetPlotLimits(ImAxis_X1, ImAxis_Y1);
  if (recenterToLive) {
    plot.followLatest = true;
    plot.hasView = false;
  }
  if (isDragging) {
    plot.followLatest = false;
  }

  // While live autofit is active, the wheel controls the amount of recent
  // history shown instead of silently dropping the plot into manual mode.
  // ImPlot has already applied its zoom by this point, so retain that span and
  // anchor it back to the latest sample on the next frame.
  if (isScrolling && plot.followLatest) {
    plot.timeWindowSeconds =
        std::clamp(limits.X.Max - limits.X.Min, PlotManager::kMinTimeWindowSeconds,
                   PlotManager::kMaxTimeWindowSeconds);
    plot.hasView = false;
    const std::string duration = formatTimeWindow(plot.timeWindowSeconds);
    ImGui::SetTooltip("Autofit window: %s\nScroll to change (0.1 s to 24 h)", duration.c_str());
  }

  plot.xMin = limits.X.Min;
  plot.xMax = limits.X.Max;
  if (!plot.followLatest) plot.hasView = true;
}

std::string signalDisplayName(Arena* arena, const PlotManager::PlotSourceRef& ref) {
  Message* msg = findMessage(arena, ref.messageId);
  Signal* sig = findSignal(arena, ref);
  if (!msg || !sig) {
    if (!ref.messageName.empty() && !ref.signalName.empty())
      return ref.messageName + " / " + ref.signalName;
    if (!ref.signalName.empty()) return ref.signalName;
    return "<unassigned>";
  }
  if (!msg->name.empty()) return msg->name + " / " + sig->name;
  char label[256]{};
  std::snprintf(label, sizeof(label), "0x%03X / %s", msg->id, sig->name.c_str());
  return label;
}

std::string makePlotTitle(Arena* arena, int typeIndex,
                          const std::vector<PlotManager::PlotSourceRef>& sources) {
  std::string sourcesPart{};
  for (size_t i = 0; i < sources.size(); ++i) {
    if (i > 0) sourcesPart += ", ";
    sourcesPart += signalDisplayName(arena, sources[i]);
  }
  if (sourcesPart.empty()) sourcesPart = "Untitled";
  return sourcesPart + " · " + kPlotSpecs[static_cast<size_t>(typeIndex)].label;
}

bool setupTimeSeriesPlot(Arena* arena, PlotManager::PlotWindow& plot, const char*& overlayText) {
  overlayText = nullptr;
  if (plot.sources.empty()) {
    ImPlot::SetNextAxisLimits(ImAxis_X1, 0.0, 1.0, ImGuiCond_Always);
    ImPlot::SetNextAxisLimits(ImAxis_Y1, 0.0, 1.0, ImGuiCond_Always);
    overlayText = "Missing data sources.";
    return false;
  }

  const double* primaryTimes = nullptr;
  const double* primaryValues = nullptr;
  int primaryCount = 0;
  if (!readFirstLiveSource(arena, plot.sources, primaryTimes, primaryValues, primaryCount)) {
    ImPlot::SetNextAxisLimits(ImAxis_X1, 0.0, 1.0, ImGuiCond_Always);
    ImPlot::SetNextAxisLimits(ImAxis_Y1, 0.0, 1.0, ImGuiCond_Always);
    overlayText = "Need live samples for the selected sources.";
    return false;
  }

  const double dataStart = primaryTimes[0];
  const double latestTime = primaryTimes[primaryCount - 1];
  plot.timeWindowSeconds =
      std::clamp(plot.timeWindowSeconds, PlotManager::kMinTimeWindowSeconds,
                 PlotManager::kMaxTimeWindowSeconds);
  const double liveWindowStart = latestTime - plot.timeWindowSeconds;
  double rangeStart = liveWindowStart;
  double rangeEnd = latestTime;
  if (!plot.followLatest && plot.hasView) {
    rangeStart = std::max(dataStart, plot.xMin);
    rangeEnd = std::max(rangeStart, plot.xMax);
    if (rangeEnd > latestTime) {
      const double span = rangeEnd - rangeStart;
      rangeEnd = latestTime;
      rangeStart = std::max(dataStart, rangeEnd - span);
    }
  }

  auto minIt = std::lower_bound(primaryTimes, primaryTimes + primaryCount, rangeStart);
  auto maxIt = std::upper_bound(primaryTimes, primaryTimes + primaryCount, rangeEnd);
  if (minIt == primaryTimes + primaryCount || minIt >= maxIt) {
    minIt = primaryTimes;
    maxIt = primaryTimes + primaryCount;
  }
  const size_t startIdx = static_cast<size_t>(minIt - primaryTimes);
  const size_t endIdx = static_cast<size_t>(maxIt - primaryTimes);
  if (startIdx >= endIdx) {
    ImPlot::SetNextAxisLimits(ImAxis_X1, 0.0, 1.0, ImGuiCond_Always);
    ImPlot::SetNextAxisLimits(ImAxis_Y1, 0.0, 1.0, ImGuiCond_Always);
    overlayText = "Not enough data in the visible range.";
    return false;
  }

  double yMin = std::numeric_limits<double>::max();
  double yMax = std::numeric_limits<double>::lowest();
  bool hasY = false;
  for (const auto& source : plot.sources) {
    const double* timeValues = nullptr;
    const double* signalValues = nullptr;
    int count = 0;
    if (!readSource(arena, source, timeValues, signalValues, count) || count < 2) continue;
    const auto sourceBegin = std::lower_bound(timeValues, timeValues + count, rangeStart);
    const auto sourceEnd = std::upper_bound(timeValues, timeValues + count, rangeEnd);
    if (sourceBegin >= sourceEnd) continue;
    for (auto value = sourceBegin; value < sourceEnd; ++value) {
      const size_t i = static_cast<size_t>(value - timeValues);
      yMin = std::min(yMin, signalValues[i]);
      yMax = std::max(yMax, signalValues[i]);
      hasY = true;
    }
  }
  if (!hasY) {
    ImPlot::SetNextAxisLimits(ImAxis_X1, 0.0, 1.0, ImGuiCond_Always);
    ImPlot::SetNextAxisLimits(ImAxis_Y1, 0.0, 1.0, ImGuiCond_Always);
    overlayText = "Selected sources have no aligned points yet.";
    return false;
  }

  if (plot.followLatest) {
    ImPlot::SetNextAxisLimits(ImAxis_X1, liveWindowStart, latestTime, ImGuiCond_Always);
  } else {
    plot.xMin = rangeStart;
    plot.xMax = rangeEnd;
    ImPlot::SetNextAxisLinks(ImAxis_X1, &plot.xMin, &plot.xMax);
  }
  ImPlot::SetNextAxisLimits(ImAxis_Y1, paddedMin(yMin, yMax), paddedMax(yMin, yMax),
                            ImGuiCond_Always);
  return true;
}

void drawTimeSeriesPlot(Arena* arena, PlotManager::PlotWindow& plot) {
  if (plot.sources.empty()) return;

  const double* primaryTimes = nullptr;
  const double* primaryValues = nullptr;
  int primaryCount = 0;
  if (!readFirstLiveSource(arena, plot.sources, primaryTimes, primaryValues, primaryCount))
    return;

  const double dataStart = primaryTimes[0];
  const double latestTime = primaryTimes[primaryCount - 1];
  const double liveWindowStart = latestTime - plot.timeWindowSeconds;
  double rangeStart = plot.followLatest ? liveWindowStart : std::max(dataStart, plot.xMin);
  double rangeEnd = plot.followLatest ? latestTime : std::max(rangeStart, plot.xMax);
  if (rangeEnd > latestTime) {
    const double span = rangeEnd - rangeStart;
    rangeEnd = latestTime;
    rangeStart = std::max(dataStart, rangeEnd - span);
  }

  auto minIt = std::lower_bound(primaryTimes, primaryTimes + primaryCount, rangeStart);
  auto maxIt = std::upper_bound(primaryTimes, primaryTimes + primaryCount, rangeEnd);
  if (minIt == primaryTimes + primaryCount || minIt >= maxIt) {
    minIt = primaryTimes;
    maxIt = primaryTimes + primaryCount;
  }
  const size_t startIdx = static_cast<size_t>(minIt - primaryTimes);
  const size_t endIdx = static_cast<size_t>(maxIt - primaryTimes);
  if (startIdx >= endIdx) return;

  for (size_t i = 0; i < plot.sources.size(); ++i) {
    const auto& source = plot.sources[i];
    const double* timeValues = nullptr;
    const double* signalValues = nullptr;
    int count = 0;
    if (!readSource(arena, source, timeValues, signalValues, count) || count < 2) continue;
    Signal* sig = findSignal(arena, source);
    if (!sig) continue;
    const auto sourceBegin = std::lower_bound(timeValues, timeValues + count, rangeStart);
    const auto sourceEnd = std::upper_bound(timeValues, timeValues + count, rangeEnd);
    if (sourceBegin >= sourceEnd) continue;
    const size_t sourceStart = static_cast<size_t>(sourceBegin - timeValues);
    const size_t sourceFinish = static_cast<size_t>(sourceEnd - timeValues);
    const RenderSlice slice = makeRenderSlice(
        sourceStart, sourceFinish,
        plot.typeIndex == PlotType_Scatter ? kMaxRenderableScatterPoints : kMaxRenderablePoints);
    if (slice.count < 2) continue;

    const char* label = sig->name.c_str();
    const int stride = static_cast<int>(sizeof(double) * slice.step);
    const ImPlotSpec strideSpec(ImPlotProp_Stride, stride);
    switch (plot.typeIndex) {
      case PlotType_Line:
        ImPlot::PlotLine(label, timeValues + slice.start, signalValues + slice.start, slice.count,
                         strideSpec);
        break;
      case PlotType_FilledLine:
        ImPlot::PlotShaded(label, timeValues + slice.start, signalValues + slice.start, slice.count,
                           0.0, strideSpec);
        ImPlot::PlotLine(label, timeValues + slice.start, signalValues + slice.start, slice.count,
                         strideSpec);
        break;
      case PlotType_Scatter:
        ImPlot::PlotScatter(label, timeValues + slice.start, signalValues + slice.start,
                            slice.count, strideSpec);
        break;
      case PlotType_Stairstep:
        ImPlot::PlotStairs(label, timeValues + slice.start, signalValues + slice.start, slice.count,
                           strideSpec);
        break;
      case PlotType_Bar:
        ImPlot::PlotBars(label, timeValues + slice.start, signalValues + slice.start, slice.count,
                         0.05, strideSpec);
        break;
      case PlotType_Stem:
        ImPlot::PlotStems(label, timeValues + slice.start, signalValues + slice.start, slice.count,
                          0.0, strideSpec);
        break;
      case PlotType_Digital:
        ImPlot::PlotDigital(label, timeValues + slice.start, signalValues + slice.start,
                            slice.count, strideSpec);
        break;
      default:
        break;
    }
  }

  const bool pairedType =
      plot.typeIndex == PlotType_Shaded || plot.typeIndex == PlotType_ErrorBars ||
      plot.typeIndex == PlotType_BarGroups || plot.typeIndex == PlotType_BarStacks;
  if (pairedType && !sharesMessageClock(plot.sources)) {
    ImPlot::PlotText("Paired sources must share a message clock until resampling is configured.",
                     rangeStart + (rangeEnd - rangeStart) * 0.5, 0.0);
    updateFollowState(plot);
    return;
  }

  if (plot.typeIndex == PlotType_Shaded && plot.sources.size() >= 2 &&
      sharesMessageClock(plot.sources)) {
    const double* t0 = nullptr;
    const double* y0 = nullptr;
    int c0 = 0;
    const double* t1 = nullptr;
    const double* y1 = nullptr;
    int c1 = 0;
    if (readSource(arena, plot.sources[0], t0, y0, c0) &&
        readSource(arena, plot.sources[1], t1, y1, c1)) {
      const size_t usableEnd = std::min({endIdx, static_cast<size_t>(c0), static_cast<size_t>(c1)});
      const RenderSlice slice = makeRenderSlice(startIdx, usableEnd);
      if (slice.count >= 2) {
        ImPlot::PlotShaded(
            "Shaded", primaryTimes + slice.start, y0 + slice.start, y1 + slice.start, slice.count,
            ImPlotSpec(ImPlotProp_Stride, static_cast<int>(sizeof(double) * slice.step)));
      }
    }
  }

  if (plot.typeIndex == PlotType_ErrorBars && plot.sources.size() >= 2 &&
      sharesMessageClock(plot.sources)) {
    const double* t0 = nullptr;
    const double* y0 = nullptr;
    int c0 = 0;
    const double* t1 = nullptr;
    const double* y1 = nullptr;
    int c1 = 0;
    if (readSource(arena, plot.sources[0], t0, y0, c0) &&
        readSource(arena, plot.sources[1], t1, y1, c1)) {
      const size_t usableEnd = std::min({endIdx, static_cast<size_t>(c0), static_cast<size_t>(c1)});
      const RenderSlice slice = makeRenderSlice(startIdx, usableEnd);
      if (slice.count >= 2) {
        Signal* base = findSignal(arena, plot.sources[0]);
        if (base) {
          const ImPlotSpec strideSpec(ImPlotProp_Stride,
                                      static_cast<int>(sizeof(double) * slice.step));
          ImPlot::PlotLine(base->name.c_str(), primaryTimes + slice.start, y0 + slice.start,
                           slice.count, strideSpec);
          ImPlot::PlotErrorBars("Error", primaryTimes + slice.start, y0 + slice.start,
                                y1 + slice.start, slice.count, strideSpec);
        }
      }
    }
  }

  if ((plot.typeIndex == PlotType_BarGroups || plot.typeIndex == PlotType_BarStacks) &&
      plot.sources.size() >= 2 && sharesMessageClock(plot.sources)) {
    std::vector<const double*> series{};
    std::vector<int> seriesCounts{};
    for (const auto& source : plot.sources) {
      const double* t = nullptr;
      const double* y = nullptr;
      int c = 0;
      if (readSource(arena, source, t, y, c) && c > 0) {
        series.push_back(y);
        seriesCounts.push_back(c);
      }
    }
    if (series.size() < 2) {
      updateFollowState(plot);
      return;
    }
    const int itemCount = static_cast<int>(series.size());
    const int groupCount = std::min(
        64, static_cast<int>(
                makeRenderSlice(startIdx,
                                std::min(endIdx, static_cast<size_t>(*std::min_element(
                                                     seriesCounts.begin(), seriesCounts.end()))),
                                512)
                    .count));
    if (groupCount > 0) {
      std::vector<double> values(static_cast<size_t>(itemCount * groupCount), 0.0);
      std::vector<const char*> labels(static_cast<size_t>(itemCount), nullptr);
      const RenderSlice slice = makeRenderSlice(startIdx, endIdx, 512);
      for (int item = 0; item < itemCount; ++item) {
        Signal* sig = findSignal(arena, plot.sources[static_cast<size_t>(item)]);
        labels[static_cast<size_t>(item)] = sig ? sig->name.c_str() : "?";
        for (int group = 0; group < groupCount; ++group)
          values[static_cast<size_t>(item * groupCount + group)] =
              series[static_cast<size_t>(item)]
                    [slice.start + static_cast<size_t>(group) * slice.step];
      }
      const ImPlotBarGroupsFlags flags = plot.typeIndex == PlotType_BarStacks
                                             ? ImPlotBarGroupsFlags_Stacked
                                             : ImPlotBarGroupsFlags_None;
      ImPlot::PlotBarGroups(labels.data(), values.data(), itemCount, groupCount, 0.67, 0.0,
                            ImPlotSpec(ImPlotProp_Flags, flags));
    }
  }

  updateFollowState(plot);
}

void renderNonTimePlot(Arena* arena, const PlotManager::PlotWindow& plot) {
  std::vector<const double*> ys{};
  std::vector<int> counts{};
  ys.reserve(plot.sources.size());
  counts.reserve(plot.sources.size());

  for (const auto& source : plot.sources) {
    const double* timeValues = nullptr;
    const double* signalValues = nullptr;
    int count = 0;
    if (!readSource(arena, source, timeValues, signalValues, count) || count <= 0) continue;
    ys.push_back(signalValues);
    counts.push_back(count);
  }
  if (ys.empty()) {
    ImGui::TextUnformatted("Need live samples for the selected sources.");
    return;
  }

  if (plot.typeIndex == PlotType_List) {
    if (ImGui::BeginTable("##plot_list", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Signal");
      ImGui::TableSetupColumn("Latest");
      ImGui::TableHeadersRow();
      for (const auto& source : plot.sources) {
        const double* timeValues = nullptr;
        const double* signalValues = nullptr;
        int count = 0;
        Signal* sig = findSignal(arena, source);
        if (!sig || !readSource(arena, source, timeValues, signalValues, count) || count <= 0)
          continue;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(sig->name.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.3f", signalValues[count - 1]);
      }
      ImGui::EndTable();
    }
    return;
  }

  if (plot.typeIndex == PlotType_Pie) {
    std::vector<double> values(ys.size(), 0.0);
    std::vector<const char*> labels(ys.size(), nullptr);
    for (size_t i = 0; i < ys.size(); ++i) {
      Signal* sig = findSignal(arena, plot.sources[i]);
      values[i] = ys[i][counts[i] - 1];
      labels[i] = sig ? sig->name.c_str() : "?";
    }
    ImPlot::PlotPieChart(labels.data(), values.data(), static_cast<int>(values.size()), 0.5, 0.5,
                         0.4, "%.2f");
    return;
  }

  if (plot.typeIndex == PlotType_Heatmap) {
    const int count = counts[0];
    if (count <= 0) return;
    const int usable = std::min(count, static_cast<int>(kMaxRenderableHeatmapCells));
    const int cols = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(usable))));
    const int rows = std::max(1, usable / cols);
    if (rows * cols > 0)
      ImPlot::PlotHeatmap("Heatmap", ys[0] + (count - (rows * cols)), rows, cols, 0.0, 0.0, "");
    return;
  }

  if (plot.typeIndex == PlotType_Histogram) {
    for (size_t i = 0; i < ys.size(); ++i) {
      Signal* sig = findSignal(arena, plot.sources[i]);
      if (sig && counts[i] > 0) ImPlot::PlotHistogram(sig->name.c_str(), ys[i], counts[i]);
    }
    return;
  }

  if (plot.typeIndex == PlotType_Histogram2D && ys.size() >= 2) {
    if (!sharesMessageClock(plot.sources)) {
      ImGui::TextUnformatted(
          "Histogram 2D sources must share a message clock until resampling is configured.");
      return;
    }
    const int count = std::min(counts[0], counts[1]);
    if (count > 1) ImPlot::PlotHistogram2D("Histogram2D", ys[0], ys[1], count);
  }
}

void render3DPlot(Arena* arena, const PlotManager::PlotWindow& plot) {
  if (plot.sources.size() < static_cast<size_t>(required3DSources(plot.useSource1TimeAsX))) {
    ImGui::TextUnformatted("Missing sources for 3D plot.");
    return;
  }
  if (!sharesMessageClock(plot.sources)) {
    ImGui::TextUnformatted("3D sources must share a message clock until resampling is configured.");
    return;
  }

  const double* x = nullptr;
  const double* y = nullptr;
  const double* z = nullptr;
  int xCount = 0;
  int yCount = 0;
  int zCount = 0;
  const double* tmpTime = nullptr;

  if (plot.useSource1TimeAsX) {
    if (!readSource(arena, plot.sources[0], x, y, yCount)) return;
    tmpTime = x;
    if (!readSource(arena, plot.sources[1], x, z, zCount)) return;
    x = tmpTime;
    xCount = yCount;
  } else {
    if (!readSource(arena, plot.sources[0], tmpTime, x, xCount)) return;
    if (!readSource(arena, plot.sources[1], tmpTime, y, yCount)) return;
    if (!readSource(arena, plot.sources[2], tmpTime, z, zCount)) return;
  }

  const int count =
      plot.useSource1TimeAsX ? std::min(yCount, zCount) : std::min({xCount, yCount, zCount});
  if (count < 2) {
    ImGui::TextUnformatted("Need live samples for the selected sources.");
    return;
  }

  Signal* s0 = findSignal(arena, plot.sources[0]);
  Signal* s1 = findSignal(arena, plot.sources[1]);
  Signal* s2 = plot.useSource1TimeAsX ? nullptr : findSignal(arena, plot.sources[2]);
  ImPlot3D::SetupAxes(
      plot.useSource1TimeAsX ? "time" : (s0 ? s0->name.c_str() : "x"),
      plot.useSource1TimeAsX ? (s0 ? s0->name.c_str() : "y") : (s1 ? s1->name.c_str() : "y"),
      plot.useSource1TimeAsX ? (s1 ? s1->name.c_str() : "z") : (s2 ? s2->name.c_str() : "z"));

  if (plot.typeIndex == PlotType_3DLine) {
    const RenderSlice slice =
        makeRenderSlice(0, static_cast<size_t>(count), kMaxRenderable3DLinePoints);
    if (slice.count >= 2)
      ImPlot3D::PlotLine(
          "3D Line", x + slice.start, y + slice.start, z + slice.start, slice.count,
          ImPlot3DSpec(ImPlot3DProp_Stride, static_cast<int>(sizeof(double) * slice.step)));
    return;
  }
  if (plot.typeIndex == PlotType_3DScatter) {
    const RenderSlice slice =
        makeRenderSlice(0, static_cast<size_t>(count), kMaxRenderable3DScatterPoints);
    if (slice.count >= 2)
      ImPlot3D::PlotScatter(
          "3D Scatter", x + slice.start, y + slice.start, z + slice.start, slice.count,
          ImPlot3DSpec(ImPlot3DProp_Stride, static_cast<int>(sizeof(double) * slice.step)));
    return;
  }
  if (plot.typeIndex == PlotType_3DSurface) {
    const int side = std::min(kMaxSurfaceSide,
                              std::max(2, static_cast<int>(std::sqrt(static_cast<double>(count)))));
    const int pointCount = side * side;
    if (pointCount >= 4 && pointCount <= count) {
      const int start = count - pointCount;
      ImPlot3D::PlotSurface("3D Surface", x + start, y + start, z + start, side, side, 0.0, 0.0,
                            ImPlot3DSpec(ImPlot3DProp_Flags, ImPlot3DSurfaceFlags_NoLines));
    }
  }
}

void renderPlotBody(Arena* arena, PlotManager::PlotWindow& plot) {
  char plotId[64]{};
  std::snprintf(plotId, sizeof(plotId), "##generated_plot_%d", plot.id);

  if (kPlotSpecs[static_cast<size_t>(plot.typeIndex)].is3D) {
    if (ImPlot3D::BeginPlot(plotId, ImVec2(-1.0f, -1.0f))) {
      render3DPlot(arena, plot);
      ImPlot3D::EndPlot();
    }
    return;
  }

  if (plot.typeIndex == PlotType_List) {
    renderNonTimePlot(arena, plot);
    return;
  }

  const char* overlayText = nullptr;
  if (kPlotSpecs[static_cast<size_t>(plot.typeIndex)].usesTimeAxis) {
    drawTimeWindowStatus(plot);
    setupTimeSeriesPlot(arena, plot, overlayText);
  }
  if (ImPlot::BeginPlot(plotId, ImVec2(-1.0f, -1.0f))) {
    if (kPlotSpecs[static_cast<size_t>(plot.typeIndex)].usesTimeAxis) {
      ImPlot::SetupAxes("time", nullptr, ImPlotAxisFlags_None, ImPlotAxisFlags_None);
      ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
      if (overlayText)
        ImPlot::PlotText(overlayText, 0.5, 0.5);
      else
        drawTimeSeriesPlot(arena, plot);
    } else {
      renderNonTimePlot(arena, plot);
    }
    ImPlot::EndPlot();
  }
}

bool matchQuery(const std::string& haystack, const char* query) {
  if (!query || query[0] == '\0') return true;
  std::string lowerHaystack = haystack;
  std::string lowerQuery = query;
  std::transform(lowerHaystack.begin(), lowerHaystack.end(), lowerHaystack.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lowerHaystack.find(lowerQuery) != std::string::npos;
}
}  // namespace

const PlotManager::PlotTypeSpec& PlotManager::specFor(int index) {
  return kPlotSpecs[static_cast<size_t>(std::clamp(index, 0, PlotType_Count - 1))];
}

const PlotManager::PlotTypeSpec& PlotManager::typeSpec(int index) { return specFor(index); }

const char* PlotManager::typeKey(int index) {
  return kPlotTypeKeys[static_cast<size_t>(std::clamp(index, 0, PlotType_Count - 1))];
}

int PlotManager::typeFromKey(std::string_view key) {
  for (size_t index = 0; index < kPlotTypeKeys.size(); ++index)
    if (key == kPlotTypeKeys[index] || key == kPlotSpecs[index].label)
      return static_cast<int>(index);
  return -1;
}

void PlotManager::init(Arena* arenaTarget) {
  if (arena == arenaTarget) return;
  arena = arenaTarget;
  arenaGeneration = arena ? arena->generation : 0;
  typeIndex = PlotType_Line;
  useSource1TimeAsX = true;
  resetPendingSourcesForType();
  refreshSignalOptions();
  refreshMatches();
}

void PlotManager::refreshForArena() {
  if (!arena || arenaGeneration == arena->generation) return;
  arenaGeneration = arena->generation;
  auto refreshRef = [this](PlotSourceRef& ref) {
    Message* message = findMessage(arena, ref.messageId);
    if (!message) {
      ref.assigned = false;
      return;
    }
    uint32_t index = SIGNAL_MAX;
    if (!ref.signalName.empty()) {
      for (uint32_t candidate = 0; candidate < message->signalCount; ++candidate) {
        Signal* signal = message->signals[candidate];
        if (signal && signal->name == ref.signalName) {
          index = candidate;
          break;
        }
      }
    }
    if (index == SIGNAL_MAX && ref.signalName.empty() && ref.signalIndex < message->signalCount &&
        message->signals[ref.signalIndex])
      index = ref.signalIndex;
    if (index == SIGNAL_MAX) {
      ref.assigned = false;
      return;
    }
    ref.signalIndex = index;
    ref.messageName = message->name;
    ref.signalName = message->signals[index]->name;
    ref.assigned = true;
  };
  for (auto& source : pendingSources) refreshRef(source);
  for (auto& window : windows)
    for (auto& source : window.sources) refreshRef(source);
  refreshSignalOptions();
  refreshMatches();
}

void PlotManager::handleHotkeys(bool homeActive) {
  if (!homeActive) return;
  ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsKeyPressed(ImGuiKey_Slash, false) && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper &&
      !io.WantTextInput) {
    openCreator();
  }
}

void PlotManager::draw(ImGuiWindowFlags flags) {
  refreshForArena();
  handleHotkeys(true);
  if (ImGui::Begin("Plots", nullptr, flags)) {
    if (ImGui::Button("Create Plot")) openCreator();
    if (!windows.empty()) {
      ImGui::SameLine();
      if (ImGui::Button("Clear All")) windows.clear();
    }
    ImGui::Separator();
    if (windows.empty()) {
      ImGui::Dummy(ImVec2(0.0f, 24.0f));
      ImGui::TextDisabled("Create a plot, then assign exact message IDs and signals.");
      ImGui::TextDisabled("Press / anywhere on this tab to open the plot creator.");
    } else {
      renderPlotWindows();
    }
  }
  ImGui::End();
  renderCreator();
}

void PlotManager::requestCreate() { openCreator(); }

void PlotManager::drawCreatedPlots() {
  refreshForArena();
  if (!windows.empty()) renderPlotWindows();
}

void PlotManager::drawCreator() {
  refreshForArena();
  renderCreator();
}

std::vector<PlotManager::PlotWindow> PlotManager::takeWindows() {
  std::vector<PlotWindow> taken = std::move(windows);
  windows.clear();
  return taken;
}

void PlotManager::renderEmbedded(PlotWindow& plot) { renderPlotBody(arena, plot); }

void PlotManager::renderHome(ImGuiID dockspaceID, const ImVec2& contentMin,
                             const ImVec2& contentMax) {
  homeDockspaceID = dockspaceID;
  refreshForArena();
  renderCreator();
  renderPlotWindows();
  if (windows.empty() && !creatorOpen) {
    const char* hint = "Press \"/\" to create plots";
    const ImVec2 textSize = ImGui::CalcTextSize(hint);
    const float xCenter = (contentMin.x + contentMax.x) * 0.5f;
    const float yTopThird = contentMin.y + ((contentMax.y - contentMin.y) * 0.28f);
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    drawList->AddText(ImVec2(xCenter - textSize.x * 0.5f, yTopThird - textSize.y * 0.5f),
                      ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.92f)), hint);
  }
}

void PlotManager::openCreator() {
  refreshForArena();
  creatorOpen = true;
  creatorFocusSearch = true;
  typeIndex = PlotType_Line;
  useSource1TimeAsX = true;
  search[0] = '\0';
  activeSourceIndex = 0;
  selectedMatch = -1;
  resetPendingSourcesForType();
  refreshMatches();
}

void PlotManager::refreshSignalOptions() {
  signalOptions.clear();
  if (!arena) return;
  for (uint32_t messageId : arena->validIds) {
    Message* msg = findMessage(arena, messageId);
    if (!msg) continue;
    for (uint32_t signalIndex = 0; signalIndex < msg->signalCount; ++signalIndex) {
      Signal* sig = msg->signals[signalIndex];
      if (!sig) continue;
      SignalOption option{};
      option.ref.messageId = messageId;
      option.ref.signalIndex = signalIndex;
      option.ref.messageName = msg->name;
      option.ref.signalName = sig->name;
      option.ref.assigned = true;
      char label[256]{};
      std::snprintf(label, sizeof(label), "0x%03X (%u) : %s / %s", messageId, messageId,
                    msg->name.c_str(), sig->name.c_str());
      option.label = label;
      signalOptions.push_back(std::move(option));
    }
  }
}

void PlotManager::refreshMatches() {
  sourceMatches.clear();
  for (size_t i = 0; i < signalOptions.size(); ++i) {
    if (matchQuery(signalOptions[i].label, search)) sourceMatches.push_back(static_cast<int>(i));
  }
  if (sourceMatches.empty())
    selectedMatch = -1;
  else
    selectedMatch = std::clamp(selectedMatch, 0, static_cast<int>(sourceMatches.size()) - 1);
}

void PlotManager::resetPendingSourcesForType() {
  const PlotTypeSpec& spec = specFor(typeIndex);
  const int count = spec.is3D ? required3DSources(useSource1TimeAsX) : spec.minSources;
  pendingSources.assign(static_cast<size_t>(count), PlotSourceRef{});
  activeSourceIndex = std::clamp(activeSourceIndex, 0, std::max(0, count - 1));
}

void PlotManager::renderCreator() {
  if (!creatorOpen) return;

  const ImGuiStyle& style = ImGui::GetStyle();
  float maxSuggestionWidth = 0.0f;
  for (int optionIndex : sourceMatches) {
    if (optionIndex < 0 || static_cast<size_t>(optionIndex) >= signalOptions.size()) continue;
    maxSuggestionWidth = std::max(
        maxSuggestionWidth,
        ImGui::CalcTextSize(signalOptions[static_cast<size_t>(optionIndex)].label.c_str()).x);
  }
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float previewColumnWidth = 430.0f;
  const float assignedColumnWidth = 300.0f;
  const float minSearchColumnWidth = 320.0f;
  const float desiredSearchColumnWidth =
      std::max(minSearchColumnWidth, maxSuggestionWidth + style.FramePadding.x * 2.0f +
                                         style.CellPadding.x * 2.0f + style.ScrollbarSize + 24.0f);
  const float availableViewportWidth =
      viewport ? viewport->WorkSize.x : ImGui::GetIO().DisplaySize.x;
  const float maxWindowWidth = std::max(720.0f, availableViewportWidth - 48.0f);
  const float desiredWindowWidth =
      std::min(maxWindowWidth, previewColumnWidth + assignedColumnWidth + desiredSearchColumnWidth +
                                   style.WindowPadding.x * 2.0f + style.CellPadding.x * 6.0f +
                                   style.ItemSpacing.x * 2.0f);
  const ImVec2 center = viewport ? viewport->GetCenter() : ImVec2(0.0f, 0.0f);
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(desiredWindowWidth, 0.0f), ImGuiCond_Always);
  bool keepOpen = true;
  const ImGuiStyle& theme = ImGui::GetStyle();
  ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.Colors[ImGuiCol_PopupBg]);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.Colors[ImGuiCol_FrameBg]);
  ImGui::PushStyleColor(ImGuiCol_Border, theme.Colors[ImGuiCol_Border]);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  if (!ImGui::Begin("Create Plot", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking)) {
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    creatorOpen = keepOpen;
    return;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) keepOpen = false;
  const PlotTypeSpec& spec = specFor(typeIndex);

  if (ImGui::BeginTable("##plot_creator", 3, ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, previewColumnWidth);
    ImGui::TableSetupColumn("Assigned", ImGuiTableColumnFlags_WidthFixed, assignedColumnWidth);
    ImGui::TableSetupColumn("Search", ImGuiTableColumnFlags_WidthFixed, desiredSearchColumnWidth);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Plot Type");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##plot_type", spec.label)) {
      for (int i = 0; i < PlotType_Count; ++i) {
        const bool selected = i == typeIndex;
        if (ImGui::Selectable(specFor(i).label, selected)) {
          typeIndex = i;
          useSource1TimeAsX = true;
          resetPendingSourcesForType();
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    if (spec.is3D && ImGui::Checkbox("Use Source 1 Time as X", &useSource1TimeAsX)) {
      resetPendingSourcesForType();
    }

    if (!spec.is3D && spec.maxSources > spec.minSources) {
      if (ImGui::Button("Add Signal") &&
          static_cast<int>(pendingSources.size()) < spec.maxSources) {
        pendingSources.push_back({});
      }
      ImGui::SameLine();
      if (ImGui::Button("Remove Signal") &&
          static_cast<int>(pendingSources.size()) > spec.minSources) {
        pendingSources.pop_back();
        activeSourceIndex = std::clamp(activeSourceIndex, 0,
                                       std::max(0, static_cast<int>(pendingSources.size()) - 1));
      }
    }

    if (ImGui::BeginChild("##plot_preview", ImVec2(0.0f, 280.0f), true)) {
      PlotWindow preview{};
      preview.id = -1;
      preview.typeIndex = typeIndex;
      preview.sources = pendingSources;
      preview.useSource1TimeAsX = useSource1TimeAsX;
      renderPlotBody(arena, preview);
    }
    ImGui::EndChild();

    ImGui::TableSetColumnIndex(1);
    ImGui::Text("Minimum Sources: %d",
                spec.is3D ? required3DSources(useSource1TimeAsX) : spec.minSources);
    ImGui::Text("Maximum Sources: %d",
                spec.is3D ? required3DSources(useSource1TimeAsX) : spec.maxSources);
    ImGui::SeparatorText("Assigned Sources");
    for (size_t i = 0; i < pendingSources.size(); ++i) {
      const bool selected = static_cast<int>(i) == activeSourceIndex;
      const std::string slotLabel = sourceSlotLabel(typeIndex, i, useSource1TimeAsX) + ": " +
                                    signalDisplayName(arena, pendingSources[i]);
      if (ImGui::Selectable(slotLabel.c_str(), selected)) activeSourceIndex = static_cast<int>(i);
    }

    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted("Search");
    if (creatorFocusSearch) {
      ImGui::SetKeyboardFocusHere();
      creatorFocusSearch = false;
    }
    if (ImGui::InputTextWithHint("##plot_search", "Search message, id, or signal", search,
                                 sizeof(search))) {
      refreshMatches();
    }
    if (ImGui::BeginChild("##plot_search_results", ImVec2(0.0f, 280.0f), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
      for (size_t i = 0; i < sourceMatches.size(); ++i) {
        const int optionIndex = sourceMatches[i];
        const bool selected = static_cast<int>(i) == selectedMatch;
        if (ImGui::Selectable(signalOptions[static_cast<size_t>(optionIndex)].label.c_str(),
                              selected, ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(maxSuggestionWidth + style.FramePadding.x * 2.0f, 0.0f))) {
          selectedMatch = static_cast<int>(i);
          pendingSources[static_cast<size_t>(activeSourceIndex)] =
              signalOptions[static_cast<size_t>(optionIndex)].ref;
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
              activeSourceIndex + 1 < static_cast<int>(pendingSources.size()))
            activeSourceIndex += 1;
        }
      }
    }
    ImGui::EndChild();

    ImGui::EndTable();
  }

  const bool canCreate = hasAllSources(pendingSources);
  if (!canCreate) ImGui::BeginDisabled();
  if (ImGui::Button("Create")) {
    createPlot();
    keepOpen = false;
  }
  if (!canCreate) ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) keepOpen = false;

  creatorOpen = keepOpen;
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);
}

void PlotManager::renderPlotWindows() {
  windows.erase(std::remove_if(windows.begin(), windows.end(),
                               [](const PlotWindow& window) { return !window.open; }),
                windows.end());
  for (PlotWindow& window : windows) {
    ImGui::PushID(window.id);
    ImGui::TextUnformatted(window.title.c_str());
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 52.0f);
    if (ImGui::SmallButton("Close")) window.open = false;
    if (ImGui::BeginChild("##plot_body", ImVec2(-1.0f, 340.0f), ImGuiChildFlags_Borders)) {
      renderPlotBody(arena, window);
    }
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::Spacing();
  }
}

void PlotManager::createPlot() {
  if (!hasAllSources(pendingSources)) return;
  PlotWindow window{};
  window.id = nextPlotId++;
  window.typeIndex = typeIndex;
  window.sources = pendingSources;
  window.useSource1TimeAsX = useSource1TimeAsX;
  window.title = makePlotTitle(arena, typeIndex, pendingSources);
  windows.push_back(std::move(window));
}

PlotManager& plotManager() {
  static PlotManager manager{};
  return manager;
}
