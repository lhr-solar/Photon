#include "plotRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "../parse/arena.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include "plots.hpp"

namespace {

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

struct ResolvedSeries {
  const PlotManager::PlotSourceRef* source = nullptr;
  const double* values = nullptr;
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
  const std::string label =
      plot.followLatest ? "AUTO  " + formatTimeWindow(plot.timeWindowSeconds) : "MANUAL";
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
  plot.timeWindowSeconds = std::clamp(plot.timeWindowSeconds, PlotManager::kMinTimeWindowSeconds,
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
  if (!readFirstLiveSource(arena, plot.sources, primaryTimes, primaryValues, primaryCount)) return;

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
    std::vector<ResolvedSeries> series{};
    series.reserve(plot.sources.size());
    for (const auto& source : plot.sources) {
      const double* t = nullptr;
      const double* y = nullptr;
      int c = 0;
      if (readSource(arena, source, t, y, c) && c > 0) series.push_back({&source, y, c});
    }
    if (series.size() < 2) {
      updateFollowState(plot);
      return;
    }
    const int itemCount = static_cast<int>(series.size());
    const auto shortest =
        std::min_element(series.begin(), series.end(),
                         [](const auto& lhs, const auto& rhs) { return lhs.count < rhs.count; });
    const size_t seriesEnd = std::min(endIdx, static_cast<size_t>(shortest->count));
    const RenderSlice slice = makeRenderSlice(startIdx, seriesEnd, 512);
    const int groupCount = std::min(64, slice.count);
    if (groupCount > 0) {
      std::vector<double> values(static_cast<size_t>(itemCount * groupCount), 0.0);
      std::vector<const char*> labels(static_cast<size_t>(itemCount), nullptr);
      for (int item = 0; item < itemCount; ++item) {
        const ResolvedSeries& itemSeries = series[static_cast<size_t>(item)];
        Signal* sig = findSignal(arena, *itemSeries.source);
        labels[static_cast<size_t>(item)] = sig ? sig->name.c_str() : "?";
        for (int group = 0; group < groupCount; ++group)
          values[static_cast<size_t>(item * groupCount + group)] =
              itemSeries.values[slice.start + static_cast<size_t>(group) * slice.step];
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
  std::vector<ResolvedSeries> series{};
  series.reserve(plot.sources.size());

  for (const auto& source : plot.sources) {
    const double* timeValues = nullptr;
    const double* signalValues = nullptr;
    int count = 0;
    if (!readSource(arena, source, timeValues, signalValues, count) || count <= 0) continue;
    series.push_back({&source, signalValues, count});
  }
  if (series.empty()) {
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
    std::vector<double> values(series.size(), 0.0);
    std::vector<const char*> labels(series.size(), nullptr);
    for (size_t i = 0; i < series.size(); ++i) {
      Signal* sig = findSignal(arena, *series[i].source);
      values[i] = series[i].values[series[i].count - 1];
      labels[i] = sig ? sig->name.c_str() : "?";
    }
    ImPlot::PlotPieChart(labels.data(), values.data(), static_cast<int>(values.size()), 0.5, 0.5,
                         0.4, "%.2f");
    return;
  }

  if (plot.typeIndex == PlotType_Heatmap) {
    const int count = series[0].count;
    if (count <= 0) return;
    const int usable = std::min(count, static_cast<int>(kMaxRenderableHeatmapCells));
    const int cols = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(usable))));
    const int rows = std::max(1, usable / cols);
    if (rows * cols > 0)
      ImPlot::PlotHeatmap("Heatmap", series[0].values + (count - (rows * cols)), rows, cols, 0.0,
                          0.0, "");
    return;
  }

  if (plot.typeIndex == PlotType_Histogram) {
    for (const ResolvedSeries& itemSeries : series) {
      Signal* sig = findSignal(arena, *itemSeries.source);
      if (sig) ImPlot::PlotHistogram(sig->name.c_str(), itemSeries.values, itemSeries.count);
    }
    return;
  }

  if (plot.typeIndex == PlotType_Histogram2D && series.size() >= 2) {
    if (!sharesMessageClock(plot.sources)) {
      ImGui::TextUnformatted(
          "Histogram 2D sources must share a message clock until resampling is configured.");
      return;
    }
    const int count = std::min(series[0].count, series[1].count);
    if (count > 1)
      ImPlot::PlotHistogram2D("Histogram2D", series[0].values, series[1].values, count);
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

  if (PlotManager::typeSpec(plot.typeIndex).is3D) {
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
  if (PlotManager::typeSpec(plot.typeIndex).usesTimeAxis) {
    drawTimeWindowStatus(plot);
    setupTimeSeriesPlot(arena, plot, overlayText);
  }
  if (ImPlot::BeginPlot(plotId, ImVec2(-1.0f, -1.0f))) {
    if (PlotManager::typeSpec(plot.typeIndex).usesTimeAxis) {
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

}  // namespace

void PlotRenderer::render(Arena* arena, PlotManager::PlotWindow& plot) {
  renderPlotBody(arena, plot);
}
