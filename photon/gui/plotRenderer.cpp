#include "plotRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../network/network.hpp"
#include "../parse/arena.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include "plots.hpp"
#include "tireSlip.hpp"

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

double transformedValue(const PlotManager::PlotSourceRef& source, double raw) {
  return raw * source.scale + source.offset;
}

bool hasDbcUnit(std::string_view unit) { return !unit.empty() && unit != "NULL"; }

std::string withDbcUnit(std::string_view name, std::string_view unit) {
  if (!hasDbcUnit(unit)) return std::string(name);
  return std::string(name) + " (" + std::string(unit) + ")";
}

std::string sourcePlotLabel(Arena* arena, const PlotManager::PlotSourceRef& source) {
  if (!source.label.empty()) {
    Signal* signal = findSignal(arena, source);
    if (signal && hasDbcUnit(signal->unit) && source.label.find(signal->unit) == std::string::npos)
      return withDbcUnit(source.label, signal->unit);
    return source.label;
  }
  Signal* signal = findSignal(arena, source);
  if (!signal) return "?";
  return withDbcUnit(signal->name, signal->unit);
}

const char* sharedYAxisUnit(Arena* arena, const std::vector<PlotManager::PlotSourceRef>& sources) {
  const char* unit = nullptr;
  for (const auto& source : sources) {
    Signal* signal = findSignal(arena, source);
    if (!signal || !hasDbcUnit(signal->unit)) return nullptr;
    if (!unit)
      unit = signal->unit.c_str();
    else if (signal->unit != unit)
      return nullptr;
  }
  return unit;
}

void fillTransformedSlice(const PlotManager::PlotSourceRef& source, const double* values,
                          const RenderSlice& slice, std::vector<double>& out) {
  out.resize(static_cast<size_t>(slice.count));
  for (int i = 0; i < slice.count; ++i)
    out[static_cast<size_t>(i)] =
        transformedValue(source, values[slice.start + static_cast<size_t>(i) * slice.step]);
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
      plot.followLatest ? "LIVE  " + formatTimeWindow(plot.timeWindowSeconds) : "MANUAL";
  const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
  const float rightAlignedX =
      ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - textSize.x);
  ImGui::SetCursorPosX(rightAlignedX);
  ImGui::TextDisabled("%s", label.c_str());
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "Scroll to zoom the live window (graph keeps moving).\n"
        "Drag to scrub the bottom timeline.\n"
        "Ctrl+click to return to live.");
}

void updateFollowState(PlotManager::PlotWindow& plot, Plots* timeline, Network* network) {
  const ImGuiIO& io = ImGui::GetIO();
  const bool hovered = ImPlot::IsPlotHovered();
  const bool isDragging =
      hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
                  ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                  ImGui::IsMouseDragging(ImGuiMouseButton_Middle));
  const float wheel = hovered ? io.MouseWheel : 0.0f;
  const bool recenterToLive = hovered && io.KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

  const ImPlotRect limits = ImPlot::GetPlotLimits(ImAxis_X1, ImAxis_Y1);
  const double visibleSeconds =
      std::clamp(limits.X.Max - limits.X.Min, PlotManager::kMinTimeWindowSeconds,
                 PlotManager::kMaxTimeWindowSeconds);

  if (recenterToLive) {
    plot.followLatest = true;
    plot.hasView = false;
    if (timeline) timeline->goLive(network);
  }

  // Live follow: wheel only changes how much history is shown. Keep streaming.
  const bool liveFollow =
      plot.followLatest || (timeline && timeline->isLive() && !isDragging && !plot.hasView);
  if (liveFollow && !isDragging) {
    plot.followLatest = true;
    plot.hasView = false;
    if (wheel != 0.0f) {
      // Match ImPlot: scroll up zooms in (smaller window).
      const double factor = wheel > 0.0f ? (1.0 / 1.1) : 1.1;
      plot.timeWindowSeconds =
          std::clamp(plot.timeWindowSeconds * factor, PlotManager::kMinTimeWindowSeconds,
                     PlotManager::kMaxTimeWindowSeconds);
      if (timeline) timeline->setViewWindowSeconds(plot.timeWindowSeconds);
      const std::string duration = formatTimeWindow(plot.timeWindowSeconds);
      ImGui::SetTooltip("Live window: %s\nScroll to zoom (graph keeps moving)", duration.c_str());
    }
    plot.xMin = limits.X.Min;
    plot.xMax = limits.X.Max;
    return;
  }

  if (isDragging) plot.followLatest = false;

  plot.xMin = limits.X.Min;
  plot.xMax = limits.X.Max;
  if (!plot.followLatest) {
    plot.hasView = true;
    // Pan or manual zoom scrubs the shared bottom timeline (right edge = cursor).
    if (timeline && (isDragging || wheel != 0.0f)) {
      plot.timeWindowSeconds = visibleSeconds;
      timeline->scrubTo(limits.X.Max, visibleSeconds, network);
    }
  }
}

bool setupTimeSeriesPlot(Arena* arena, PlotManager::PlotWindow& plot, const char*& overlayText,
                         Plots* timeline) {
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
  // Bottom timeline cursor is authoritative when scrubbed/paused/playing.
  // Keep the plot's own zoom level so Play/scrub does not snap the window back in.
  if (timeline && !timeline->isLive()) {
    plot.followLatest = false;
    plot.hasView = true;
    plot.xMax = timeline->cursor;
    plot.xMin = plot.xMax - plot.timeWindowSeconds;
  } else if (timeline && timeline->isLive()) {
    plot.followLatest = true;
    plot.hasView = false;
  }

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
      const double sample = transformedValue(source, signalValues[i]);
      yMin = std::min(yMin, sample);
      yMax = std::max(yMax, sample);
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

bool readMotorVelocityTimes(Arena* arena, const double*& times, int& count) {
  times = nullptr;
  count = 0;
  Message* message = findMessage(arena, 1059);
  if (!message) return false;
  count = static_cast<int>(message->signalSize.value.load(std::memory_order_acquire) / sizeof(double));
  if (count < 2) return false;
  times = static_cast<const double*>(message->timeData);
  return times != nullptr;
}

bool setupTireSlipPlot(Arena* arena, PlotManager::PlotWindow& plot, const char*& overlayText,
                       Plots* timeline) {
  overlayText = nullptr;
  const double* primaryTimes = nullptr;
  int primaryCount = 0;
  if (!readMotorVelocityTimes(arena, primaryTimes, primaryCount)) {
    ImPlot::SetNextAxisLimits(ImAxis_X1, 0.0, 1.0, ImGuiCond_Always);
    ImPlot::SetNextAxisLimits(ImAxis_Y1, 0.0, 1.0, ImGuiCond_Always);
    overlayText = "Need MC_MotorVelocity samples for tire slip.";
    return false;
  }

  const double dataStart = primaryTimes[0];
  const double latestTime = primaryTimes[primaryCount - 1];
  if (timeline && !timeline->isLive()) {
    plot.followLatest = false;
    plot.hasView = true;
    plot.xMax = timeline->cursor;
    plot.xMin = plot.xMax - plot.timeWindowSeconds;
  } else if (timeline && timeline->isLive()) {
    plot.followLatest = true;
    plot.hasView = false;
  }

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

  std::array<double, kMaxRenderablePoints> times{};
  std::array<double, kMaxRenderablePoints> slipGps{};
  std::array<double, kMaxRenderablePoints> slipMc{};
  const size_t written =
      tireSlipSeries(*arena, rangeStart, rangeEnd, times.data(), slipGps.data(), slipMc.data(),
                     times.size());
  if (written < 2) {
    ImPlot::SetNextAxisLimits(ImAxis_X1, 0.0, 1.0, ImGuiCond_Always);
    ImPlot::SetNextAxisLimits(ImAxis_Y1, 0.0, 1.0, ImGuiCond_Always);
    overlayText = "Not enough tire-slip samples in the visible range.";
    return false;
  }

  double yMin = std::numeric_limits<double>::max();
  double yMax = std::numeric_limits<double>::lowest();
  bool hasY = false;
  for (size_t i = 0; i < written; ++i) {
    for (double sample : {slipGps[i], slipMc[i]}) {
      if (!std::isfinite(sample)) continue;
      yMin = std::min(yMin, sample);
      yMax = std::max(yMax, sample);
      hasY = true;
    }
  }
  if (!hasY) {
    ImPlot::SetNextAxisLimits(ImAxis_X1, 0.0, 1.0, ImGuiCond_Always);
    ImPlot::SetNextAxisLimits(ImAxis_Y1, -5.0, 5.0, ImGuiCond_Always);
    overlayText = "Tire slip needs ground speed (GPS or MC_VehicleVelocity).";
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

void drawTireSlipPlot(Arena* arena, PlotManager::PlotWindow& plot, Plots* timeline,
                      Network* network) {
  const double* primaryTimes = nullptr;
  int primaryCount = 0;
  if (!readMotorVelocityTimes(arena, primaryTimes, primaryCount)) return;

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

  std::array<double, kMaxRenderablePoints> times{};
  std::array<double, kMaxRenderablePoints> slipGps{};
  std::array<double, kMaxRenderablePoints> slipMc{};
  const size_t written =
      tireSlipSeries(*arena, rangeStart, rangeEnd, times.data(), slipGps.data(), slipMc.data(),
                     times.size());
  if (written < 2) return;

  ImPlot::PlotLine("Slip vs GPS (%)", times.data(), slipGps.data(), static_cast<int>(written));
  ImPlot::PlotLine("Slip vs MC (%)", times.data(), slipMc.data(), static_cast<int>(written));
  updateFollowState(plot, timeline, network);
}

void drawXyScatterPlot(Arena* arena, PlotManager::PlotWindow& plot, Plots* timeline,
                       Network* network) {
  if (plot.sources.size() < 2) return;
  if (!sharesMessageClock(plot.sources)) {
    ImGui::TextUnformatted(
        "XY scatter sources must share a message clock until resampling is configured.");
    return;
  }

  const double* xTimes = nullptr;
  const double* yTimes = nullptr;
  const double* xValues = nullptr;
  const double* yValues = nullptr;
  int xCount = 0;
  int yCount = 0;
  if (!readSource(arena, plot.sources[0], xTimes, xValues, xCount) ||
      !readSource(arena, plot.sources[1], yTimes, yValues, yCount))
    return;
  const int count = std::min(xCount, yCount);
  if (count < 2 || !xTimes) return;

  const double latestTime = xTimes[count - 1];
  const double rangeStart =
      plot.followLatest ? latestTime - plot.timeWindowSeconds : std::max(xTimes[0], plot.xMin);
  const double rangeEnd = plot.followLatest ? latestTime : std::max(rangeStart, plot.xMax);
  auto minIt = std::lower_bound(xTimes, xTimes + count, rangeStart);
  auto maxIt = std::upper_bound(xTimes, xTimes + count, rangeEnd);
  if (minIt >= maxIt) {
    minIt = xTimes;
    maxIt = xTimes + count;
  }
  const RenderSlice slice = makeRenderSlice(static_cast<size_t>(minIt - xTimes),
                                            static_cast<size_t>(maxIt - xTimes),
                                            kMaxRenderableScatterPoints);
  if (slice.count < 2) return;

  std::vector<double> xs;
  std::vector<double> ys;
  fillTransformedSlice(plot.sources[0], xValues, slice, xs);
  fillTransformedSlice(plot.sources[1], yValues, slice, ys);

  double xMin = xs.front();
  double xMax = xs.front();
  double yMin = ys.front();
  double yMax = ys.front();
  for (size_t i = 0; i < xs.size(); ++i) {
    xMin = std::min(xMin, xs[i]);
    xMax = std::max(xMax, xs[i]);
    yMin = std::min(yMin, ys[i]);
    yMax = std::max(yMax, ys[i]);
  }

  ImPlot::SetupAxes(sourcePlotLabel(arena, plot.sources[0]).c_str(),
                    sourcePlotLabel(arena, plot.sources[1]).c_str());
  ImPlot::SetupAxesLimits(paddedMin(xMin, xMax), paddedMax(xMin, xMax), paddedMin(yMin, yMax),
                          paddedMax(yMin, yMax), ImGuiCond_Always);
  ImPlot::PlotScatter(plot.title.empty() ? "GG" : plot.title.c_str(), xs.data(), ys.data(),
                      static_cast<int>(xs.size()));
  updateFollowState(plot, timeline, network);
}

void drawTimeSeriesPlot(Arena* arena, PlotManager::PlotWindow& plot, Plots* timeline,
                        Network* network) {
  if (plot.sources.empty()) return;
  if (plot.typeIndex == PlotType_Scatter && !plot.useSource1TimeAsX && plot.sources.size() >= 2) {
    drawXyScatterPlot(arena, plot, timeline, network);
    return;
  }

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

    const std::string label = sourcePlotLabel(arena, source);
    const bool identityTransform = source.scale == 1.0 && source.offset == 0.0;
    std::vector<double> transformed;
    std::vector<double> sampledTimes;
    const double* plotTimes = timeValues + slice.start;
    const double* plotValues = signalValues + slice.start;
    int plotCount = slice.count;
    int stride = static_cast<int>(sizeof(double) * slice.step);
    if (!identityTransform) {
      fillTransformedSlice(source, signalValues, slice, transformed);
      sampledTimes.resize(static_cast<size_t>(slice.count));
      for (int sample = 0; sample < slice.count; ++sample)
        sampledTimes[static_cast<size_t>(sample)] =
            timeValues[slice.start + static_cast<size_t>(sample) * slice.step];
      plotTimes = sampledTimes.data();
      plotValues = transformed.data();
      plotCount = static_cast<int>(transformed.size());
      stride = static_cast<int>(sizeof(double));
    }
    const ImPlotSpec strideSpec(ImPlotProp_Stride, stride);
    switch (plot.typeIndex) {
      case PlotType_Line:
        ImPlot::PlotLine(label.c_str(), plotTimes, plotValues, plotCount, strideSpec);
        break;
      case PlotType_FilledLine:
        ImPlot::PlotShaded(label.c_str(), plotTimes, plotValues, plotCount, 0.0, strideSpec);
        ImPlot::PlotLine(label.c_str(), plotTimes, plotValues, plotCount, strideSpec);
        break;
      case PlotType_Scatter:
        ImPlot::PlotScatter(label.c_str(), plotTimes, plotValues, plotCount, strideSpec);
        break;
      case PlotType_Stairstep:
        ImPlot::PlotStairs(label.c_str(), plotTimes, plotValues, plotCount, strideSpec);
        break;
      case PlotType_Bar:
        ImPlot::PlotBars(label.c_str(), plotTimes, plotValues, plotCount, 0.05, strideSpec);
        break;
      case PlotType_Stem:
        ImPlot::PlotStems(label.c_str(), plotTimes, plotValues, plotCount, 0.0, strideSpec);
        break;
      case PlotType_Digital:
        ImPlot::PlotDigital(label.c_str(), plotTimes, plotValues, plotCount, strideSpec);
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
    updateFollowState(plot, timeline, network);
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
      updateFollowState(plot, timeline, network);
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

  updateFollowState(plot, timeline, network);
}

double physicalStateValue(const Signal& signal, int64_t rawValue) {
  return static_cast<double>(rawValue) * signal.scale + signal.offset;
}

const SignalValueDescription* currentState(const Signal& signal, double physicalValue) {
  if (signal.scale == 0.0) return nullptr;
  const int64_t rawValue =
      static_cast<int64_t>(std::llround((physicalValue - signal.offset) / signal.scale));
  for (const auto& description : signal.valueDescriptions)
    if (description.rawValue == rawValue) return &description;
  return nullptr;
}

void renderListPlot(Arena* arena, Network* network, const PlotManager::PlotWindow& plot) {
  static std::unordered_map<ImGuiID, double> editValues{};
  if (!ImGui::BeginTable("##plot_list", 3,
                         ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                             ImGuiTableFlags_SizingStretchProp))
    return;

  ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_WidthStretch, 1.3f);
  const char* latestHeader = "Latest";
  char latestHeaderBuf[64]{};
  if (const char* unit = sharedYAxisUnit(arena, plot.sources)) {
    std::snprintf(latestHeaderBuf, sizeof(latestHeaderBuf), "Latest (%s)", unit);
    latestHeader = latestHeaderBuf;
  }
  ImGui::TableSetupColumn(latestHeader, ImGuiTableColumnFlags_WidthStretch, 1.0f);
  ImGui::TableSetupColumn("Set via CAN", ImGuiTableColumnFlags_WidthStretch, 1.4f);
  ImGui::TableHeadersRow();
  const bool canTransmit = network && network->canSendCAN();

  for (const auto& source : plot.sources) {
    Message* message = findMessage(arena, source.messageId);
    Signal* signal = findSignal(arena, source);
    const uint32_t signalIndex = resolvedSignalIndex(arena, source);
    if (!message || !signal || signalIndex == SIGNAL_MAX) continue;

    const double* timeValues = nullptr;
    const double* signalValues = nullptr;
    int count = 0;
    const bool hasValue = readSource(arena, source, timeValues, signalValues, count) && count > 0;
    const double latest = hasValue ? signalValues[count - 1] : 0.0;
    ImGui::PushID(static_cast<int>(source.messageId));
    ImGui::PushID(static_cast<int>(signalIndex));
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(withDbcUnit(signal->name, signal->unit).c_str());

    ImGui::TableSetColumnIndex(1);
    if (!hasValue) {
      ImGui::TextDisabled("No samples");
    } else if (const auto* state = currentState(*signal, latest)) {
      ImGui::TextUnformatted(state->label.c_str());
    } else if (hasDbcUnit(signal->unit)) {
      ImGui::Text("%.3f %s", latest, signal->unit.c_str());
    } else {
      ImGui::Text("%.3f", latest);
    }

    ImGui::TableSetColumnIndex(2);
    if (!canTransmit) ImGui::BeginDisabled();
    if (!signal->valueDescriptions.empty()) {
      const SignalValueDescription* selected = hasValue ? currentState(*signal, latest) : nullptr;
      const char* preview = selected ? selected->label.c_str() : "Select state";
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::BeginCombo("##state", preview)) {
        for (const auto& description : signal->valueDescriptions) {
          const bool isSelected = selected && selected->rawValue == description.rawValue;
          if (ImGui::Selectable(description.label.c_str(), isSelected)) {
            network->sendDBCSignal(message->name, signal->name,
                                   physicalStateValue(*signal, description.rawValue));
          }
          if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    } else if (signal->length == 1 && signal->scale == 1.0 && signal->offset == 0.0) {
      // A DBC bit with no named VAL_ table is still a semantic on/off control;
      // do not expose its encoded 0/1 value to the user.
      const bool enabled = hasValue && latest >= 0.5;
      const char* action = enabled ? "Turn off" : "Turn on";
      if (ImGui::Button(action, {-1.0f, 0.0f}))
        network->sendDBCSignal(message->name, signal->name, enabled ? 0.0 : 1.0);
    } else {
      const ImGuiID editId = ImGui::GetID("##value");
      auto [edit, inserted] = editValues.try_emplace(editId, hasValue ? latest : signal->min);
      const double step = std::abs(signal->scale) > 0.0 ? std::abs(signal->scale) : 0.0;
      const float buttonWidth = 48.0f;
      ImGui::SetNextItemWidth(std::max(
          40.0f, ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x));
      ImGui::InputDouble("##value", &edit->second, step, step * 10.0, "%.6g");
      ImGui::SameLine();
      if (ImGui::Button("Set", {buttonWidth, 0.0f}))
        network->sendDBCSignal(message->name, signal->name, edit->second);
      if (signal->max > signal->min && ImGui::IsItemHovered())
        ImGui::SetTooltip("DBC range: %.6g to %.6g", signal->min, signal->max);
    }
    if (!canTransmit) {
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Connect PCAN with Listen only disabled, then choose Arm CAN writes in Network.");
    }
    ImGui::PopID();
    ImGui::PopID();
  }
  ImGui::EndTable();
}

void renderNonTimePlot(Arena* arena, Network* network, const PlotManager::PlotWindow& plot) {
  if (plot.typeIndex == PlotType_List) {
    renderListPlot(arena, network, plot);
    return;
  }

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

void renderPlotBody(Arena* arena, Network* network, PlotManager::PlotWindow& plot,
                    Plots* timeline) {
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
    renderNonTimePlot(arena, network, plot);
    return;
  }

  const char* overlayText = nullptr;
  const bool xyScatter =
      plot.typeIndex == PlotType_Scatter && !plot.useSource1TimeAsX && plot.sources.size() >= 2;
  const bool tireSlip = plot.typeIndex == PlotType_TireSlip;
  if (tireSlip) {
    drawTimeWindowStatus(plot);
    setupTireSlipPlot(arena, plot, overlayText, timeline);
  } else if (PlotManager::typeSpec(plot.typeIndex).usesTimeAxis && !xyScatter) {
    drawTimeWindowStatus(plot);
    setupTimeSeriesPlot(arena, plot, overlayText, timeline);
  }
  if (ImPlot::BeginPlot(plotId, ImVec2(-1.0f, -1.0f))) {
    if (xyScatter) {
      drawXyScatterPlot(arena, plot, timeline, network);
    } else if (tireSlip) {
      ImPlot::SetupAxes("time", "%",
                        plot.followLatest ? ImPlotAxisFlags_Lock : ImPlotAxisFlags_None,
                        ImPlotAxisFlags_None);
      ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
      if (overlayText)
        ImPlot::PlotText(overlayText, 0.5, 0.5);
      else
        drawTireSlipPlot(arena, plot, timeline, network);
    } else if (PlotManager::typeSpec(plot.typeIndex).usesTimeAxis) {
      const char* yAxis = sharedYAxisUnit(arena, plot.sources);
      // Lock X while live so ImPlot wheel-zoom cannot freeze the right edge behind "now".
      // Wheel is handled in updateFollowState to resize the live window instead.
      const ImPlotAxisFlags xFlags =
          plot.followLatest ? ImPlotAxisFlags_Lock : ImPlotAxisFlags_None;
      ImPlot::SetupAxes("time", yAxis, xFlags, ImPlotAxisFlags_None);
      ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
      if (overlayText)
        ImPlot::PlotText(overlayText, 0.5, 0.5);
      else
        drawTimeSeriesPlot(arena, plot, timeline, network);
    } else {
      renderNonTimePlot(arena, network, plot);
    }
    ImPlot::EndPlot();
  }
}

}  // namespace

void PlotRenderer::render(Arena* arena, Network* network, PlotManager::PlotWindow& plot,
                          Plots* timeline) {
  renderPlotBody(arena, network, plot, timeline);
}
