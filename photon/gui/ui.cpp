#include "ui.hpp"
#include "../engine/include.hpp"
#include "imgui.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstddef>
#include <cfloat>
#include <cwctype>
#include <sstream>
#include <iomanip>
#include <limits>
#include <unordered_map>
#include <array>
#include <cstdio>
#include "console.hpp"
#include "imgui_internal.h"
#include "implot.h"
#include "implot3d.h"

namespace {
struct PlotDataSourceRef {
    int canId = -1;
    int signalIndex = -1;
};

struct RenderSlice {
    size_t start = 0;
    size_t end = 0;
    size_t step = 1;
    int count = 0;
};

constexpr size_t kMaxRenderablePoints = 4096;
constexpr size_t kMaxRenderablePoints3DLine = 1024;
constexpr size_t kMaxRenderablePoints3DScatter = 256;
constexpr int kMaxSurfaceSide = 24;

RenderSlice makeRenderSlice(size_t start, size_t end, size_t maxPoints = kMaxRenderablePoints) {
    RenderSlice slice;
    slice.start = start;
    slice.end = end;
    if (end <= start) { return slice; }
    const size_t total = end - start;
    slice.step = std::max<size_t>(1, (total + maxPoints - 1) / maxPoints);
    slice.count = static_cast<int>((total + slice.step - 1) / slice.step);
    return slice;
}

struct SignalOption {
    PlotDataSourceRef ref;
    std::string label;
};

struct PlotTypeSpec {
    const char* label;
    int minSources;
    int maxSources;
    bool usesTimeAxis;
    bool is3D;
};

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
    PlotType_Count
};

constexpr std::array<PlotTypeSpec, PlotType_Count> kPlotSpecs{{
    {"Line Plots", 1, 8, true, false},
    {"Filled Line Plots", 1, 8, true, false},
    {"Shaded Plots", 2, 2, true, false},
    {"Scatter Plots", 1, 8, true, false},
    {"Stairstep Plots", 1, 8, true, false},
    {"Bar Plots", 1, 8, true, false},
    {"Bar Groups", 2, 8, true, false},
    {"Bar Stacks", 2, 8, true, false},
    {"Error Bars", 2, 2, true, false},
    {"Stem Plots", 1, 8, true, false},
    {"Pie Charts", 1, 8, false, false},
    {"Heatmaps", 1, 1, false, false},
    {"Histogram", 1, 8, false, false},
    {"Histogram 2D", 2, 2, false, false},
    {"Digital Plots", 1, 8, true, false},
    {"3D Line Plots", 2, 2, false, true},
    {"3D Scatter Plots", 3, 3, false, true},
    {"3D Surface Plots", 3, 3, false, true}
}};

struct GeneratedPlotWindow {
    int id = 0;
    int typeIndex = PlotType_Line;
    std::vector<PlotDataSourceRef> sources;
    bool open = true;
    bool followLatest = true;
    bool hasView = false;
    double xMin = 0.0;
    double xMax = 0.0;
};

struct PlotGeneratorState {
    bool creating = false;
    int typeIndex = PlotType_Line;
    std::vector<PlotDataSourceRef> sources;
    int nextId = 1;
    std::vector<GeneratedPlotWindow> windows;
};

PlotGeneratorState& generatorState() {
    static PlotGeneratorState state;
    return state;
}

const PlotTypeSpec& specFor(int typeIndex) {
    return kPlotSpecs[static_cast<size_t>(typeIndex)];
}

void resetGeneratorSourcesForType(PlotGeneratorState& state) {
    const PlotTypeSpec& spec = specFor(state.typeIndex);
    if (state.sources.size() < static_cast<size_t>(spec.minSources)) {
        state.sources.resize(static_cast<size_t>(spec.minSources));
    }
    if (state.sources.size() > static_cast<size_t>(spec.maxSources)) {
        state.sources.resize(static_cast<size_t>(spec.maxSources));
    }
}

std::vector<SignalOption> collectSignalOptions(Parse* parseINTF) {
    std::vector<SignalOption> options;
    if (!parseINTF) { return options; }
    options.reserve(parseINTF->canStore.canMessages.size() * 8);

    for (const auto& [canId, msg] : parseINTF->canStore.canMessages) {
        for (size_t i = 0; i < msg.signals.size(); ++i) {
            char idBuf[16] = {0};
            std::snprintf(idBuf, sizeof(idBuf), "0x%03X", canId);
            SignalOption opt;
            opt.ref.canId = canId;
            opt.ref.signalIndex = static_cast<int>(i);
            opt.label = std::string(idBuf) + " : " + msg.name + " / " + msg.signals[i].name;
            options.push_back(std::move(opt));
        }
    }
    return options;
}

const CanMessage* findMessage(Parse* parseINTF, int canId) {
    if (!parseINTF) { return nullptr; }
    auto it = parseINTF->canStore.canMessages.find(canId);
    return (it == parseINTF->canStore.canMessages.end()) ? nullptr : &it->second;
}

const CanSignal* findSignal(Parse* parseINTF, const PlotDataSourceRef& src) {
    const CanMessage* msg = findMessage(parseINTF, src.canId);
    if (!msg) { return nullptr; }
    if (src.signalIndex < 0 || static_cast<size_t>(src.signalIndex) >= msg->signals.size()) { return nullptr; }
    return &msg->signals[static_cast<size_t>(src.signalIndex)];
}

std::string sourceName(Parse* parseINTF, const PlotDataSourceRef& src) {
    const CanMessage* msg = findMessage(parseINTF, src.canId);
    if (!msg) { return "<missing message>"; }
    if (src.signalIndex < 0 || static_cast<size_t>(src.signalIndex) >= msg->signals.size()) { return "<missing signal>"; }
    char idBuf[16] = {0};
    std::snprintf(idBuf, sizeof(idBuf), "0x%03X", src.canId);
    return std::string(idBuf) + "/" + msg->signals[static_cast<size_t>(src.signalIndex)].name;
}

int findOptionIndex(const std::vector<SignalOption>& options, const PlotDataSourceRef& src) {
    for (size_t i = 0; i < options.size(); ++i) {
        if (options[i].ref.canId == src.canId && options[i].ref.signalIndex == src.signalIndex) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void updateFollowState(GeneratedPlotWindow& plot) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool isNavigating =
        ImPlot::IsPlotHovered() &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
         ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
         ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
         io.MouseWheel != 0.0f ||
         io.MouseWheelH != 0.0f);
    const bool recenterToLive =
        ImPlot::IsPlotHovered() &&
        io.KeyCtrl &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (recenterToLive) {
        plot.followLatest = true;
        plot.hasView = false;
    }
    if (isNavigating) {
        plot.followLatest = false;
    }

    const ImPlotRect limits = ImPlot::GetPlotLimits(ImAxis_X1, ImAxis_Y1);
    plot.xMin = limits.X.Min;
    plot.xMax = limits.X.Max;
    plot.hasView = true;
}

void renderTimeSeriesPlot(Parse* parseINTF, GeneratedPlotWindow& plot) {
    if (plot.sources.empty()) {
        ImGui::TextUnformatted("Missing data sources.");
        return;
    }
    const CanMessage* primaryMsg = findMessage(parseINTF, plot.sources[0].canId);
    if (!primaryMsg) {
        ImGui::TextUnformatted("Primary data source is no longer available.");
        return;
    }
    const std::vector<double>& xAxis = primaryMsg->time;
    if (xAxis.size() < 2) {
        ImGui::TextUnformatted("Need at least 2 time points.");
        return;
    }

    constexpr double maxTime = 5.0;
    const double dataStart = xAxis.front();
    const double latestTime = xAxis.back();
    const double liveWindowStart = std::max(dataStart, latestTime - maxTime);

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

    auto minIt = std::lower_bound(xAxis.begin(), xAxis.end(), rangeStart);
    auto maxIt = std::upper_bound(xAxis.begin(), xAxis.end(), rangeEnd);
    if (minIt == xAxis.end() || minIt >= maxIt) {
        minIt = xAxis.begin();
        maxIt = xAxis.end();
    }
    const size_t startIdx = static_cast<size_t>(std::distance(xAxis.begin(), minIt));
    const size_t endIdx = static_cast<size_t>(std::distance(xAxis.begin(), maxIt));
    if (startIdx >= endIdx) {
        ImGui::TextUnformatted("Not enough data in the visible range.");
        return;
    }

    bool hasY = false;
    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
    std::vector<const CanSignal*> signals;
    signals.reserve(plot.sources.size());
    for (const PlotDataSourceRef& src : plot.sources) {
        const CanSignal* signal = findSignal(parseINTF, src);
        if (!signal) { continue; }
        signals.push_back(signal);
        const size_t usableEnd = std::min(endIdx, signal->data.size());
        if (startIdx >= usableEnd) { continue; }
        for (size_t i = startIdx; i < usableEnd; ++i) {
            yMin = std::min(yMin, signal->data[i]);
            yMax = std::max(yMax, signal->data[i]);
            hasY = true;
        }
    }
    if (!hasY) {
        ImGui::TextUnformatted("Selected sources have no aligned points yet.");
        return;
    }

    if (std::abs(yMax - yMin) < 1e-6) {
        const double span = std::max(1.0, std::abs(yMax));
        yMin -= span * 0.5;
        yMax += span * 0.5;
    }
    const double pad = (yMax - yMin) * 0.1;
    yMin -= pad;
    yMax += pad;

    if (plot.followLatest) {
        ImPlot::SetNextAxisLimits(ImAxis_X1, liveWindowStart, latestTime, ImPlotCond_Always);
    }
    ImPlot::SetNextAxisLimits(ImAxis_Y1, yMin, yMax, ImPlotCond_Always);

    std::string plotId = "##generatedPlot_" + std::to_string(plot.id);
    if (!ImPlot::BeginPlot(plotId.c_str(), ImVec2(-FLT_MIN, -FLT_MIN))) {
        return;
    }

    switch (plot.typeIndex) {
        case PlotType_Line: {
            for (const CanSignal* signal : signals) {
                const size_t usableEnd = std::min(endIdx, signal->data.size());
                if (usableEnd <= startIdx) { continue; }
                const RenderSlice slice = makeRenderSlice(startIdx, usableEnd);
                if (slice.count > 1) {
                    ImPlot::PlotLine(signal->name.c_str(), xAxis.data() + slice.start, signal->data.data() + slice.start,
                                     slice.count, 0, 0, static_cast<int>(sizeof(double) * slice.step));
                }
            }
            break;
        }
        case PlotType_FilledLine: {
            for (const CanSignal* signal : signals) {
                const size_t usableEnd = std::min(endIdx, signal->data.size());
                if (usableEnd <= startIdx) { continue; }
                const RenderSlice slice = makeRenderSlice(startIdx, usableEnd);
                if (slice.count > 1) {
                    ImPlot::PlotShaded(signal->name.c_str(), xAxis.data() + slice.start, signal->data.data() + slice.start,
                                       slice.count, 0.0, 0, 0, static_cast<int>(sizeof(double) * slice.step));
                    ImPlot::PlotLine(signal->name.c_str(), xAxis.data() + slice.start, signal->data.data() + slice.start,
                                     slice.count, 0, 0, static_cast<int>(sizeof(double) * slice.step));
                }
            }
            break;
        }
        case PlotType_Shaded: {
            if (signals.size() >= 2) {
                const size_t usableEnd = std::min({endIdx, signals[0]->data.size(), signals[1]->data.size()});
                if (usableEnd <= startIdx) { break; }
                const RenderSlice slice = makeRenderSlice(startIdx, usableEnd);
                if (slice.count > 1) {
                    ImPlot::PlotShaded("Shaded", xAxis.data() + slice.start, signals[0]->data.data() + slice.start,
                                       signals[1]->data.data() + slice.start, slice.count, 0, 0,
                                       static_cast<int>(sizeof(double) * slice.step));
                    ImPlot::PlotLine(signals[0]->name.c_str(), xAxis.data() + slice.start, signals[0]->data.data() + slice.start,
                                     slice.count, 0, 0, static_cast<int>(sizeof(double) * slice.step));
                    ImPlot::PlotLine(signals[1]->name.c_str(), xAxis.data() + slice.start, signals[1]->data.data() + slice.start,
                                     slice.count, 0, 0, static_cast<int>(sizeof(double) * slice.step));
                }
            }
            break;
        }
        case PlotType_Scatter: {
            for (const CanSignal* signal : signals) {
                const size_t usableEnd = std::min(endIdx, signal->data.size());
                if (usableEnd <= startIdx) { continue; }
                const RenderSlice slice = makeRenderSlice(startIdx, usableEnd);
                if (slice.count > 1) {
                    ImPlot::PlotScatter(signal->name.c_str(), xAxis.data() + slice.start, signal->data.data() + slice.start,
                                        slice.count, 0, 0, static_cast<int>(sizeof(double) * slice.step));
                }
            }
            break;
        }
        case PlotType_Stairstep: {
            for (const CanSignal* signal : signals) {
                const size_t usableEnd = std::min(endIdx, signal->data.size());
                if (usableEnd <= startIdx) { continue; }
                const RenderSlice slice = makeRenderSlice(startIdx, usableEnd);
                if (slice.count > 1) {
                    ImPlot::PlotStairs(signal->name.c_str(), xAxis.data() + slice.start, signal->data.data() + slice.start,
                                       slice.count, 0, 0, static_cast<int>(sizeof(double) * slice.step));
                }
            }
            break;
        }
        case PlotType_Bar: {
            for (const CanSignal* signal : signals) {
                const size_t usableEnd = std::min(endIdx, signal->data.size());
                if (usableEnd <= startIdx) { continue; }
                const RenderSlice slice = makeRenderSlice(startIdx, usableEnd);
                if (slice.count > 1) {
                    ImPlot::PlotBars(signal->name.c_str(), xAxis.data() + slice.start, signal->data.data() + slice.start,
                                     slice.count, 0.05, 0, 0, static_cast<int>(sizeof(double) * slice.step));
                }
            }
            break;
        }
        case PlotType_BarGroups:
        case PlotType_BarStacks: {
            int itemCount = static_cast<int>(signals.size());
            const RenderSlice slice = makeRenderSlice(startIdx, endIdx, 512);
            int groupCount = slice.count;
            if (itemCount > 0 && groupCount > 0) {
                std::vector<double> values(static_cast<size_t>(itemCount * groupCount), 0.0);
                std::vector<const char*> labels(static_cast<size_t>(itemCount), nullptr);
                std::vector<std::string> labelStorage(static_cast<size_t>(itemCount));
                for (int item = 0; item < itemCount; ++item) {
                    labelStorage[static_cast<size_t>(item)] = signals[static_cast<size_t>(item)]->name;
                    labels[static_cast<size_t>(item)] = labelStorage[static_cast<size_t>(item)].c_str();
                    for (int group = 0; group < groupCount; ++group) {
                        const size_t idx = slice.start + static_cast<size_t>(group) * slice.step;
                        if (idx < signals[static_cast<size_t>(item)]->data.size()) {
                            values[static_cast<size_t>(item * groupCount + group)] = signals[static_cast<size_t>(item)]->data[idx];
                        }
                    }
                }
                ImPlotBarGroupsFlags flags = ImPlotBarGroupsFlags_None;
                if (plot.typeIndex == PlotType_BarStacks) {
                    flags |= ImPlotBarGroupsFlags_Stacked;
                }
                ImPlot::PlotBarGroups(labels.data(), values.data(), itemCount, groupCount, 0.67, 0.0, flags);
            }
            break;
        }
        case PlotType_ErrorBars: {
            if (signals.size() >= 2) {
                const size_t usableEnd = std::min({endIdx, signals[0]->data.size(), signals[1]->data.size()});
                if (usableEnd <= startIdx) { break; }
                const RenderSlice slice = makeRenderSlice(startIdx, usableEnd);
                if (slice.count > 1) {
                    ImPlot::PlotLine(signals[0]->name.c_str(), xAxis.data() + slice.start, signals[0]->data.data() + slice.start,
                                     slice.count, 0, 0, static_cast<int>(sizeof(double) * slice.step));
                    ImPlot::PlotErrorBars("Error", xAxis.data() + slice.start, signals[0]->data.data() + slice.start,
                                          signals[1]->data.data() + slice.start, slice.count, 0, 0,
                                          static_cast<int>(sizeof(double) * slice.step));
                }
            }
            break;
        }
        case PlotType_Stem: {
            for (const CanSignal* signal : signals) {
                const size_t usableEnd = std::min(endIdx, signal->data.size());
                if (usableEnd <= startIdx) { continue; }
                const RenderSlice slice = makeRenderSlice(startIdx, usableEnd);
                if (slice.count > 1) {
                    ImPlot::PlotStems(signal->name.c_str(), xAxis.data() + slice.start, signal->data.data() + slice.start,
                                      slice.count, 0.0, 0, 0, static_cast<int>(sizeof(double) * slice.step));
                }
            }
            break;
        }
        case PlotType_Digital: {
            for (const CanSignal* signal : signals) {
                const size_t usableEnd = std::min(endIdx, signal->data.size());
                if (usableEnd <= startIdx) { continue; }
                const RenderSlice slice = makeRenderSlice(startIdx, usableEnd);
                if (slice.count > 1) {
                    ImPlot::PlotDigital(signal->name.c_str(), xAxis.data() + slice.start, signal->data.data() + slice.start,
                                        slice.count, 0, 0, static_cast<int>(sizeof(double) * slice.step));
                }
            }
            break;
        }
        default: break;
    }

    updateFollowState(plot);
    ImPlot::EndPlot();
}

void renderNonTimePlot(Parse* parseINTF, GeneratedPlotWindow& plot) {
    std::vector<const CanSignal*> signals;
    signals.reserve(plot.sources.size());
    for (const PlotDataSourceRef& src : plot.sources) {
        const CanSignal* signal = findSignal(parseINTF, src);
        if (signal) { signals.push_back(signal); }
    }
    if (signals.empty()) {
        ImGui::TextUnformatted("No valid data sources for this plot.");
        return;
    }

    std::string plotId = "##generatedPlot_" + std::to_string(plot.id);
    if (!ImPlot::BeginPlot(plotId.c_str(), ImVec2(-FLT_MIN, -FLT_MIN))) {
        return;
    }

    switch (plot.typeIndex) {
        case PlotType_Pie: {
            std::vector<double> values(signals.size(), 0.0);
            std::vector<const char*> labels(signals.size(), nullptr);
            std::vector<std::string> labelStorage(signals.size());
            for (size_t i = 0; i < signals.size(); ++i) {
                const auto& data = signals[i]->data;
                values[i] = data.empty() ? 0.0 : data.back();
                labelStorage[i] = signals[i]->name;
                labels[i] = labelStorage[i].c_str();
            }
            ImPlot::PlotPieChart(labels.data(), values.data(), static_cast<int>(values.size()), 0.5, 0.5, 0.4, "%.2f");
            break;
        }
        case PlotType_Heatmap: {
            const std::vector<double>& v = signals[0]->data;
            if (v.empty()) { break; }
            const size_t start = (v.size() > kMaxRenderablePoints) ? (v.size() - kMaxRenderablePoints) : 0;
            const size_t usable = v.size() - start;
            const int cols = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(usable))));
            const int rows = std::max(1, static_cast<int>(usable / static_cast<size_t>(cols)));
            const int count = rows * cols;
            if (count > 0) {
                ImPlot::PlotHeatmap(signals[0]->name.c_str(), v.data() + start, rows, cols, 0.0, 0.0, "%.2f");
            }
            break;
        }
        case PlotType_Histogram: {
            for (const CanSignal* signal : signals) {
                if (!signal->data.empty()) {
                    ImPlot::PlotHistogram(signal->name.c_str(), signal->data.data(), static_cast<int>(signal->data.size()));
                }
            }
            break;
        }
        case PlotType_Histogram2D: {
            if (signals.size() >= 2) {
                const int count = static_cast<int>(std::min(signals[0]->data.size(), signals[1]->data.size()));
                if (count > 1) {
                    ImPlot::PlotHistogram2D("Histogram2D", signals[0]->data.data(), signals[1]->data.data(), count);
                }
            }
            break;
        }
        default: break;
    }

    ImPlot::EndPlot();
}

void render3DPlot(Parse* parseINTF, GeneratedPlotWindow& plot) {
    static int lastFrame = -1;
    static int frameBudget = 12000;
    const int frameNow = ImGui::GetFrameCount();
    if (frameNow != lastFrame) {
        lastFrame = frameNow;
        frameBudget = 12000;
    }
    auto clampByBudget = [&](int wantedCost, int units, int minUnits, int costPerUnit) -> int {
        if (units <= 0 || costPerUnit <= 0) { return 0; }
        const int allowed = std::max(minUnits, std::min(units, frameBudget / costPerUnit));
        const int actualCost = std::min(wantedCost, allowed * costPerUnit);
        frameBudget = std::max(0, frameBudget - actualCost);
        return allowed;
    };

    std::string plotId = "##generatedPlot3D_" + std::to_string(plot.id);
    if (!ImPlot3D::BeginPlot(plotId.c_str(), ImVec2(-FLT_MIN, -FLT_MIN), ImPlot3DFlags_None)) {
        return;
    }

    if (plot.typeIndex == PlotType_3DLine) {
        if (plot.sources.size() < 2) {
            ImPlot3D::EndPlot();
            ImGui::TextUnformatted("3D line requires 2 sources (Y, Z).");
            return;
        }
        const CanMessage* timeMsg = findMessage(parseINTF, plot.sources[0].canId);
        const CanSignal* ySignal = findSignal(parseINTF, plot.sources[0]);
        const CanSignal* zSignal = findSignal(parseINTF, plot.sources[1]);
        if (!timeMsg || !ySignal || !zSignal) {
            ImPlot3D::EndPlot();
            ImGui::TextUnformatted("Missing source(s) for 3D line.");
            return;
        }

        const std::vector<double>& xs = timeMsg->time;      // X defaults to first selected signal's message time
        const std::vector<double>& ys = ySignal->data;      // Y from source 1 signal
        const std::vector<double>& zs = zSignal->data;      // Z from source 2 signal
        const size_t count = std::min({xs.size(), ys.size(), zs.size()});
        if (count < 2) {
            ImPlot3D::EndPlot();
            ImGui::TextUnformatted("Need at least 2 points for 3D line.");
            return;
        }

        constexpr double maxTime = 5.0;
        const double latestTime = xs[count - 1];
        const double windowStart = std::max(xs[0], latestTime - maxTime);
        auto minIt = std::lower_bound(xs.begin(), xs.begin() + static_cast<std::ptrdiff_t>(count), windowStart);
        const size_t startIdx = static_cast<size_t>(std::distance(xs.begin(), minIt));
        const RenderSlice slice = makeRenderSlice(startIdx, count, kMaxRenderablePoints3DLine);
        const int allowed = clampByBudget(slice.count * 2, slice.count, 2, 2);

        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();
        double zMin = std::numeric_limits<double>::max();
        double zMax = std::numeric_limits<double>::lowest();
        for (size_t i = slice.start; i < count; i += slice.step) {
            yMin = std::min(yMin, ys[i]);
            yMax = std::max(yMax, ys[i]);
            zMin = std::min(zMin, zs[i]);
            zMax = std::max(zMax, zs[i]);
        }
        if (std::abs(yMax - yMin) < 1e-6) { yMin -= 0.5; yMax += 0.5; }
        if (std::abs(zMax - zMin) < 1e-6) { zMin -= 0.5; zMax += 0.5; }
        const double yPad = (yMax - yMin) * 0.1;
        const double zPad = (zMax - zMin) * 0.1;
        ImPlot3D::SetupAxes("time", ySignal->name.c_str(), zSignal->name.c_str());
        ImPlot3D::SetupAxesLimits(windowStart, latestTime, yMin - yPad, yMax + yPad, zMin - zPad, zMax + zPad, ImPlot3DCond_Always);

        if (allowed > 1) {
            ImPlot3D::PlotLine("3D Line", xs.data() + slice.start, ys.data() + slice.start, zs.data() + slice.start,
                               allowed, 0, 0, static_cast<int>(sizeof(double) * slice.step));
        }
    } else {
        std::vector<const CanSignal*> signals;
        signals.reserve(plot.sources.size());
        for (const PlotDataSourceRef& src : plot.sources) {
            const CanSignal* signal = findSignal(parseINTF, src);
            if (signal) { signals.push_back(signal); }
        }
        if (signals.size() < 3) {
            ImPlot3D::EndPlot();
            ImGui::TextUnformatted("3D plots require 3 valid sources (X, Y, Z).");
            return;
        }
        const std::vector<double>& xs = signals[0]->data;
        const std::vector<double>& ys = signals[1]->data;
        const std::vector<double>& zs = signals[2]->data;
        const size_t count = std::min({xs.size(), ys.size(), zs.size()});
        if (count < 2) {
            ImPlot3D::EndPlot();
            ImGui::TextUnformatted("Need at least 2 points for 3D plotting.");
            return;
        }
        ImPlot3D::SetupAxes(signals[0]->name.c_str(), signals[1]->name.c_str(), signals[2]->name.c_str());

        if (plot.typeIndex == PlotType_3DScatter) {
        const size_t start = (count > kMaxRenderablePoints3DScatter) ? (count - kMaxRenderablePoints3DScatter) : 0;
        const RenderSlice slice = makeRenderSlice(start, count, kMaxRenderablePoints3DScatter);
        const int allowed = clampByBudget(slice.count * 12, slice.count, 2, 12);
        if (allowed > 1) {
            ImPlot3D::PlotScatter("3D Scatter", xs.data() + slice.start, ys.data() + slice.start, zs.data() + slice.start,
                                  allowed, 0, 0, static_cast<int>(sizeof(double) * slice.step));
        }
        } else if (plot.typeIndex == PlotType_3DSurface) {
        const size_t start = (count > static_cast<size_t>(kMaxSurfaceSide * kMaxSurfaceSide))
            ? (count - static_cast<size_t>(kMaxSurfaceSide * kMaxSurfaceSide))
            : 0;
        const size_t usable = count - start;
        const int side = std::min(kMaxSurfaceSide, std::max(2, static_cast<int>(std::sqrt(static_cast<double>(usable)))));
        int pointCount = side * side;
        int allowedPointCount = clampByBudget(pointCount * 8, pointCount, 4, 8);
        int allowedSide = static_cast<int>(std::sqrt(static_cast<double>(allowedPointCount)));
        allowedSide = std::max(2, std::min(side, allowedSide));
        pointCount = allowedSide * allowedSide;
        if (pointCount >= 4 && static_cast<size_t>(pointCount) <= usable) {
            // Draw filled surface only (no wireframe) to keep vertex count bounded.
            ImPlot3D::PlotSurface("3D Surface",
                                  xs.data() + (count - static_cast<size_t>(pointCount)),
                                  ys.data() + (count - static_cast<size_t>(pointCount)),
                                  zs.data() + (count - static_cast<size_t>(pointCount)),
                                  allowedSide, allowedSide, 0.0, 0.0, ImPlot3DSurfaceFlags_NoLines);
        }
        }
    }

    ImPlot3D::EndPlot();
}

void drawGeneratedPlots(Parse* parseINTF) {
    PlotGeneratorState& state = generatorState();
    for (GeneratedPlotWindow& plot : state.windows) {
        std::string windowTitle = std::string(specFor(plot.typeIndex).label) + " #" + std::to_string(plot.id);
        if (ImGui::Begin(windowTitle.c_str(), &plot.open)) {
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            if (specFor(plot.typeIndex).is3D &&
                ((plot.typeIndex == PlotType_3DLine && plot.sources.size() >= 2) ||
                 (plot.typeIndex != PlotType_3DLine && plot.sources.size() >= 3))) {
                if (plot.typeIndex == PlotType_3DLine) {
                    ImGui::Text("X(time): message time from %s", sourceName(parseINTF, plot.sources[0]).c_str());
                    ImGui::Text("Y: %s", sourceName(parseINTF, plot.sources[0]).c_str());
                    ImGui::Text("Z: %s", sourceName(parseINTF, plot.sources[1]).c_str());
                } else {
                    ImGui::Text("X: %s", sourceName(parseINTF, plot.sources[0]).c_str());
                    ImGui::Text("Y: %s", sourceName(parseINTF, plot.sources[1]).c_str());
                    ImGui::Text("Z: %s", sourceName(parseINTF, plot.sources[2]).c_str());
                }
            } else {
                for (size_t i = 0; i < plot.sources.size(); ++i) {
                    ImGui::Text("Source %zu: %s", i + 1, sourceName(parseINTF, plot.sources[i]).c_str());
                }
            }
            ImGui::Separator();
            if (specFor(plot.typeIndex).is3D) {
                render3DPlot(parseINTF, plot);
            } else if (specFor(plot.typeIndex).usesTimeAxis) {
                renderTimeSeriesPlot(parseINTF, plot);
            } else {
                renderNonTimePlot(parseINTF, plot);
            }
        }
        ImGui::End();
    }
    state.windows.erase(std::remove_if(state.windows.begin(), state.windows.end(),
        [](const GeneratedPlotWindow& window) { return !window.open; }),
        state.windows.end());
}

void drawGeneratorUI(Parse* parseINTF) {
    PlotGeneratorState& state = generatorState();
    const std::vector<SignalOption> options = collectSignalOptions(parseINTF);

    if (!state.creating) {
        if (ImGui::Button("Create New")) {
            state.creating = true;
            state.typeIndex = PlotType_Line;
            state.sources.assign(static_cast<size_t>(specFor(state.typeIndex).minSources), PlotDataSourceRef{});
        }
        ImGui::Text("Created plots: %zu", state.windows.size());
        return;
    }

    ImGui::SeparatorText("Plot Generator");
    if (ImGui::BeginCombo("Plot Type", specFor(state.typeIndex).label)) {
        for (int i = 0; i < PlotType_Count; ++i) {
            const bool selected = (state.typeIndex == i);
            if (ImGui::Selectable(specFor(i).label, selected)) {
                state.typeIndex = i;
                resetGeneratorSourcesForType(state);
            }
            if (selected) { ImGui::SetItemDefaultFocus(); }
        }
        ImGui::EndCombo();
    }

    const PlotTypeSpec& spec = specFor(state.typeIndex);
    ImGui::Text("Data sources required: %d", spec.minSources);
    ImGui::Text("Data sources allowed: %d to %d", spec.minSources, spec.maxSources);
    if (spec.is3D) {
        if (state.typeIndex == PlotType_3DLine) {
            ImGui::TextUnformatted("3D line mapping: X = Source 1 time, Y = Source 1, Z = Source 2");
        } else {
            ImGui::TextUnformatted("3D source mapping: Source 1 = X, Source 2 = Y, Source 3 = Z");
        }
    } else {
        ImGui::Text("Time source (if used): Source 1");
    }

    if (spec.maxSources > spec.minSources) {
        if (ImGui::Button("Add Source") && state.sources.size() < static_cast<size_t>(spec.maxSources)) {
            state.sources.push_back({});
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove Source") && state.sources.size() > static_cast<size_t>(spec.minSources)) {
            state.sources.pop_back();
        }
    }

    if (options.empty()) {
        ImGui::TextUnformatted("No CAN signals available yet.");
    }

    for (size_t i = 0; i < state.sources.size(); ++i) {
        int selectedIndex = findOptionIndex(options, state.sources[i]);
        std::string currentLabel = (selectedIndex >= 0) ? options[static_cast<size_t>(selectedIndex)].label : "<select source>";
        std::string comboLabel;
        if (state.typeIndex == PlotType_3DLine) {
            comboLabel = (i == 0) ? "Y Source" : "Z Source";
        } else {
            comboLabel = "Source " + std::to_string(i + 1);
        }
        if (ImGui::BeginCombo(comboLabel.c_str(), currentLabel.c_str())) {
            for (size_t optionIndex = 0; optionIndex < options.size(); ++optionIndex) {
                const bool selected = (selectedIndex == static_cast<int>(optionIndex));
                if (ImGui::Selectable(options[optionIndex].label.c_str(), selected)) {
                    state.sources[i] = options[optionIndex].ref;
                    selectedIndex = static_cast<int>(optionIndex);
                }
                if (selected) { ImGui::SetItemDefaultFocus(); }
            }
            ImGui::EndCombo();
        }
    }

    bool allSelected = !state.sources.empty();
    for (const PlotDataSourceRef& src : state.sources) {
        if (findOptionIndex(options, src) < 0) {
            allSelected = false;
            break;
        }
    }
    const bool validCount = state.sources.size() >= static_cast<size_t>(spec.minSources) &&
                            state.sources.size() <= static_cast<size_t>(spec.maxSources);
    const bool canCreate = allSelected && validCount;

    if (!canCreate) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Create")) {
        GeneratedPlotWindow window;
        window.id = state.nextId++;
        window.typeIndex = state.typeIndex;
        window.sources = state.sources;
        state.windows.push_back(std::move(window));
        state.creating = false;
    }
    if (!canCreate) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        state.creating = false;
    }
}
} // namespace

void UI::build(){
    ImGuiIO &io = ImGui::GetIO();
    static bool showFps = false;
    ImGui::NewFrame();
    background();
    for (auto& [id, msg] : parseINTF->canStore.canMessages) msg.updateMessage(parseINTF);

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::Begin("Root", nullptr, host_flags);
    ImGuiID root_id = ImGui::GetID("RootDockspace");
    ImGui::DockSpace(root_id);
    ImGui::End();

    ImGuiWindowFlags big_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoBackground;

    auto fitToViewport = [&]() {
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
    };

    /*
    fitToViewport();
    ImGuiID main_dock_id = 0;
    if (ImGui::Begin("Main", nullptr, big_flags)) {
        main_dock_id = ImGui::GetID("MainDockspace");
        ImGui::DockSpace(main_dock_id);
    } ImGui::End();

    fitToViewport();
    ImGuiID custom_dock_id = 0;
    if (ImGui::Begin("Custom", nullptr, big_flags)) {
        custom_dock_id = ImGui::GetID("CustomDockspace");
        ImGui::DockSpace(custom_dock_id);
        // if we have "Custom" selected, then we should dock in windows that we want 
        // e.g. we want to dock in "drawGeneratorUI(parseINTF)"
        // however, if it is not selected, e.g. I am on "Main"
        // none of these things should exist/be visible
        // all the elements should be placed in the dockspace within "Custom"
        // of course, like normal docked elements, they should be 
        // moveable, resizeable, adapt to other docked elements, etc
        //
        // we should also be able to dock into it
        // once something is docked in, it "inherits" the property that it is only shown
        // when the main window is selected/currently being shown
        // it is possible that we may need to switch our format for our main windows, so that it is just 3 Windows docked into eachother, 
        // that are able to be tabbed through
        //
        // some things I want in the "custom" window
        // drawGeneratorUI(parseINTF);
        // if(!parseINTF->canStore.canMessages.empty())
        //  dynamicInlinePlot(parseINTF->canStore.canMessages[0x02].time, 
        //  parseINTF->canStore.canMessages[0x02].signals[0].data, "ts");
    } ImGui::End();

    fitToViewport();
    ImGuiID debug_dock_id = 0;
    if (ImGui::Begin("Debug", nullptr, big_flags)) {
        debug_dock_id = ImGui::GetID("DebugDockspace");
        ImGui::DockSpace(debug_dock_id);
    }
    ImGui::End();
    */

    fitToViewport();
    if(ImGui::Begin("Main", nullptr, big_flags)){
        ImGui::Text("some text");
        drawGeneratorUI(parseINTF);
        if(!parseINTF->canStore.canMessages.empty())
            dynamicInlinePlot(parseINTF->canStore.canMessages[0x02].time, 
                    parseINTF->canStore.canMessages[0x02].signals[0].data, "ts");

    } ImGui::End();

    drawGeneratedPlots(parseINTF);
    signalSearch();

    if(ImGui::IsKeyReleased(ImGuiKey_F3)) showFps = !showFps;
    if(showFps) fpsWindow();
    ImGui::Render();
}

void UI::genericInlinePlot(const std::vector<double>& xAxis, const std::vector<double>& yAxis, const char* name){
    if (xAxis.size() < 2 || yAxis.size() != xAxis.size()) { return; }
    constexpr double maxTime = 5.0;
    const double windowStart = std::max(0.0, xAxis.back() - maxTime);
    auto startIt = std::lower_bound(xAxis.begin(), xAxis.end(), windowStart);
    const std::size_t startIdx = static_cast<std::size_t>(std::distance(xAxis.begin(), startIt));

    if (startIdx >= xAxis.size()) { return; }

    double currentMin = yAxis[startIdx];
    double currentMax = yAxis[startIdx];
    for (std::size_t i = startIdx; i < yAxis.size(); ++i) {
        currentMin = std::min(currentMin, yAxis[i]);
        currentMax = std::max(currentMax, yAxis[i]);
    }
    if (std::abs(currentMax - currentMin) < 1e-3) {
        double span = std::max(1.0, std::abs(currentMax));
        currentMin -= span * 0.5;
        currentMax += span * 0.5;
    }
    double pad = (currentMax - currentMin) * 0.1;
    double yMin = currentMin - pad;
    double yMax = currentMax + pad;

    ImPlot::SetNextAxisLimits(ImAxis_X1, windowStart, xAxis.back(), ImPlotCond_Always);
    ImPlot::SetNextAxisLimits(ImAxis_Y1, yMin, yMax, ImPlotCond_Always);
    std::string plotLabel = std::string(name) + "##inlinePlot";

    if (ImPlot::BeginPlot(plotLabel.c_str(), ImVec2(-FLT_MIN, 200.0f), ImPlotFlags_NoLegend)) {
        const RenderSlice slice = makeRenderSlice(startIdx, xAxis.size());
        const double* xData = xAxis.data() + slice.start;
        const double* yData = yAxis.data() + slice.start;
        const int count = slice.count;
        ImPlot::SetNextLineStyle({1.0, 1.0, 1.0, 1.0});
        ImPlot::PlotLine(name, xData, yData, count, 0, 0, static_cast<int>(sizeof(double) * slice.step));
        ImPlot::EndPlot();
    }
}

void UI::dynamicInlinePlot(const std::vector<double>& xAxis, const std::vector<double>& yAxis, const char* name){
    if (xAxis.size() < 2 || yAxis.size() != xAxis.size()) { return; }

    struct PlotState {
        bool followLatest = true;
        bool hasView = false;
        double xMin = 0.0;
        double xMax = 0.0;
    };
    static std::unordered_map<std::string, PlotState> plotStates;

    constexpr double maxTime = 5.0;
    const double dataStart = xAxis.front();
    const double latestTime = xAxis.back();
    const double liveWindowStart = std::max(dataStart, latestTime - maxTime);
    const std::string key(name);
    PlotState& state = plotStates[key];

    double rangeStart = liveWindowStart;
    double rangeEnd = latestTime;
    if (!state.followLatest && state.hasView) {
        rangeStart = std::max(dataStart, state.xMin);
        rangeEnd = std::max(rangeStart, state.xMax);
        if (rangeEnd > latestTime) {
            const double span = rangeEnd - rangeStart;
            rangeEnd = latestTime;
            rangeStart = std::max(dataStart, rangeEnd - span);
        }
    }

    auto minIt = std::lower_bound(xAxis.begin(), xAxis.end(), rangeStart);
    auto maxIt = std::upper_bound(xAxis.begin(), xAxis.end(), rangeEnd);
    if (minIt == xAxis.end() || minIt >= maxIt) {
        minIt = xAxis.begin();
        maxIt = xAxis.end();
    }
    const std::size_t startIdx = static_cast<std::size_t>(std::distance(xAxis.begin(), minIt));
    const std::size_t endIdx = static_cast<std::size_t>(std::distance(xAxis.begin(), maxIt));

    double currentMin = yAxis[startIdx];
    double currentMax = yAxis[startIdx];
    for (std::size_t i = startIdx; i < endIdx; ++i) {
        currentMin = std::min(currentMin, yAxis[i]);
        currentMax = std::max(currentMax, yAxis[i]);
    }
    if (std::abs(currentMax - currentMin) < 1e-3) {
        const double span = std::max(1.0, std::abs(currentMax));
        currentMin -= span * 0.5;
        currentMax += span * 0.5;
    }
    const double pad = (currentMax - currentMin) * 0.1;
    const double yMin = currentMin - pad;
    const double yMax = currentMax + pad;

    if (state.followLatest) {
        ImPlot::SetNextAxisLimits(ImAxis_X1, liveWindowStart, latestTime, ImPlotCond_Always);
    }
    ImPlot::SetNextAxisLimits(ImAxis_Y1, yMin, yMax, ImPlotCond_Always);

    std::string plotLabel = std::string(name) + "##dynamicInlinePlot";
    if (ImPlot::BeginPlot(plotLabel.c_str(), ImVec2(-FLT_MIN, 200.0f), ImPlotFlags_NoLegend)) {
        ImPlot::SetNextLineStyle({1.0, 1.0, 1.0, 1.0});
        const RenderSlice slice = makeRenderSlice(startIdx, endIdx);
        if (slice.count > 1) {
            ImPlot::PlotLine(name, xAxis.data() + slice.start, yAxis.data() + slice.start,
                             slice.count, 0, 0, static_cast<int>(sizeof(double) * slice.step));
        }

        const ImGuiIO& io = ImGui::GetIO();
        const bool isNavigating =
            ImPlot::IsPlotHovered() &&
            (ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
             ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
             ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
             io.MouseWheel != 0.0f ||
             io.MouseWheelH != 0.0f);
        const bool recenterToLive =
            ImPlot::IsPlotHovered() &&
            io.KeyCtrl &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (recenterToLive) {
            state.followLatest = true;
            state.hasView = false;
        }
        if (isNavigating) {
            state.followLatest = false;
        }

        const ImPlotRect limits = ImPlot::GetPlotLimits(ImAxis_X1, ImAxis_Y1);
        state.xMin = limits.X.Min;
        state.xMax = limits.X.Max;
        state.hasView = true;

        ImPlot::EndPlot();
    }
}

void UI::fpsWindow(){
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 padding(12.0f, 12.0f);
    ImVec2 windowPos = ImVec2(io.DisplaySize.x - padding.x, padding.y);
    ImVec2 windowPivot = ImVec2(1.0f, 0.0f);
    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, windowPivot);
    ImGui::SetNextWindowBgAlpha(0.0f);

    float ft = io.DeltaTime * 1000.0f;
    for (size_t i = 1; i < renderSettings.frameTimes.size(); ++i) {
        renderSettings.frameTimes[i - 1] = renderSettings.frameTimes[i];
    }
    renderSettings.frameTimes[renderSettings.frameTimes.size() - 1] = ft;
    renderSettings.frameTimeMin = 9999.0f;
    renderSettings.frameTimeMax = 0.0f;
    for (float v : renderSettings.frameTimes) {
        renderSettings.frameTimeMin = std::min(renderSettings.frameTimeMin, v);
        renderSettings.frameTimeMax = std::max(renderSettings.frameTimeMax, v);
    }
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoBackground |
                                   ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    // Stats window
    if (ImGui::Begin("Photon Stats", NULL, windowFlags)) {
        ImGuiIO &io = ImGui::GetIO();
        float fps = io.Framerate;
        float ft_ms = (io.DeltaTime > 0.0f) ? (io.DeltaTime * 1000.0f) : 0.0f;
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Text("Frame time: %.3f ms", ft_ms);
        ImGui::Separator();
        ImGui::Text("Device Name: %s", deviceName[0] ? deviceName : "Unknown");
        ImGui::Text("VendorID: 0x%04X  DeviceID: 0x%04X", vendorID, deviceID);
        const char* typeStr = "Other";
        switch (deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: typeStr = "Integrated GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: typeStr = "Discrete GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: typeStr = "Virtual GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU: typeStr = "CPU"; break;
            default: break;
        }
        ImGui::Text("Device Type: %s", typeStr);
        ImGui::Text("Driver: %u  API: %u.%u.%u",
            driverVersion,
            VK_API_VERSION_MAJOR(apiVersion),
            VK_API_VERSION_MINOR(apiVersion),
            VK_API_VERSION_PATCH(apiVersion));
        ImGui::Separator();
        ImGui::Text("Frametime (last %zu):", renderSettings.frameTimes.size());
        ImGui::PlotLines("##ft", renderSettings.frameTimes.data(), (int)renderSettings.frameTimes.size(), 0,
                         NULL, renderSettings.frameTimeMin, renderSettings.frameTimeMax,
                         ImVec2(240, 80));
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
}

void UI::signalSearch(){
    if(!cmdOpen && ImGui::IsKeyPressed(ImGuiKey_Slash)){
        cmdOpen = true;
        cmdFF = true;
        cmdBuffer[0] = '\0';
        cmdResults.clear();
        cmdSelected = -1;
        cmdShowPopup = false;
    }

    if(!cmdOpen && !cmdShowPopup) { return; }

    if(ImGui::IsKeyPressed(ImGuiKey_Escape)){
        cmdOpen = false;
        cmdShowPopup = false;
        return;
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 center = vp ? vp->GetCenter() : ImVec2(0, 0);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav;

    bool windowFocused = false;
    bool inputActive = false;
    bool hidePrompt = false;

    if(cmdOpen){
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
        ImGui::SetNextWindowBgAlpha(0.50);
        if(ImGui::Begin("CommandPrompt", nullptr, flags)){
            ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
            if(cmdFF){ ImGui::SetKeyboardFocusHere(); }
            bool submitted = ImGui::InputText("##cmdInput", cmdBuffer, IM_ARRAYSIZE(cmdBuffer), inputFlags);
            if(cmdFF){ cmdFF = false; }
            search();
            windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            inputActive = ImGui::IsItemActive();

            const int resultCount = static_cast<int>(cmdResults.size());
            if(cmdResults.size()>0){
                if(cmdSelected < 0) cmdSelected = 0;
                const int previousSelected = cmdSelected;
                if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)) cmdSelected = (cmdSelected + 1) % resultCount;
                if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)) cmdSelected = (cmdSelected - 1 + resultCount) % resultCount;
                if(ImGui::IsKeyPressed(ImGuiKey_Tab)) cmdSelected = (cmdSelected + 1) % resultCount;
                const bool selectionMovedByKeyboard = (cmdSelected != previousSelected);

                float rowHeight = ImGui::GetTextLineHeightWithSpacing();
                constexpr int visibleRows = 5;
                const int rowsToShow = std::min(resultCount, visibleRows);
                ImVec2 listSize(ImGui::GetContentRegionAvail().x, rowHeight * rowsToShow + ImGui::GetStyle().FramePadding.y);
                if(ImGui::BeginListBox("##cmdResults", listSize)){
                    for(int i = 0; i < resultCount; i++){
                        const bool isSelected = (i == cmdSelected);
                        if(ImGui::Selectable(cmdResults[i].name.data(), isSelected)){
                            cmdSelected = i;
                            activeCmdResult = cmdResults[i];
                            cmdShowPopup = true;
                            hidePrompt = true;
                        }
                        if (isSelected && selectionMovedByKeyboard) {
                            ImGui::SetScrollHereY();
                        }
                    }
                    ImGui::EndListBox();
                }
                bool activateSelection = (submitted || ImGui::IsKeyPressed(ImGuiKey_Enter) 
                                                    || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
                if(activateSelection && cmdSelected >= 0 && cmdSelected < resultCount){
                    activeCmdResult = cmdResults[cmdSelected];
                    cmdShowPopup = true;
                    hidePrompt = true;
                }
            } else { cmdSelected = -1; }
        } ImGui::End();
        ImGui::PopStyleColor(2);
    }
    bool popupFocused = false;
    if(cmdShowPopup){ popupFocused = popupWindow(); } // this is what you are looking for
    if(hidePrompt){ cmdOpen = false; }
    if(!windowFocused && !inputActive && !popupFocused){
        cmdOpen = false;
        cmdShowPopup = false;
    }
}

bool UI::popupWindow(){
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | 
                             ImGuiWindowFlags_NoSavedSettings  | 
                             ImGuiWindowFlags_NoTitleBar | 
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoMove;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
    ImGui::SetNextWindowBgAlpha(0.50);

    bool focused = false;
    bool childFocused = false;
    static int selected = 0;
    static ImVec2 popupWindowSize(360.0f, 420.0f);
    static ImVec2 popupWideSize(900.0f, 330.0f);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!vp) {
        ImGui::PopStyleColor(2);
        return false;
    }
    ImVec2 center = vp->GetCenter();
    const float gap = 20.0f;
    auto clampPos = [&](const ImVec2& pos, const ImVec2& size) {
        const float minX = vp->Pos.x + 8.0f;
        const float minY = vp->Pos.y + 8.0f;
        const float maxX = vp->Pos.x + vp->Size.x - size.x - 8.0f;
        const float maxY = vp->Pos.y + vp->Size.y - size.y - 8.0f;
        return ImVec2(std::clamp(pos.x, minX, std::max(minX, maxX)),
                      std::clamp(pos.y, minY, std::max(minY, maxY)));
    };
    const float totalWidth = popupWindowSize.x + gap + popupWideSize.x;
    ImVec2 popupWindowPos(center.x - totalWidth * 0.5f, center.y - popupWindowSize.y * 0.5f);
    popupWindowPos = clampPos(popupWindowPos, popupWindowSize);
    ImGui::SetNextWindowPos(popupWindowPos, ImGuiCond_Always);
    const CanMessage& msg = parseINTF->canStore.canMessages[activeCmdResult.canID];
    if (msg.signals.empty()) {
        ImGui::Text("No signals available");
        ImGui::PopStyleColor(2);
        return focused;
    }
    selected = std::clamp(selected, 0, static_cast<int>(msg.signals.size()) - 1);
    if(ImGui::Begin("Command Result", &cmdShowPopup, flags)){
        focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        popupWindowSize = ImGui::GetWindowSize();
        ImGui::Text("Message Name: %s", msg.name.c_str());
        ImGui::Text("CanID: %#04x", msg.canId);
        ImGui::Text("DLC: %d", msg.dlc);
        ImGui::Text("Transmitter: %s", msg.transmitter.c_str());
        ImGui::Text("Data Rate: %.0f B/s", msg.dataRate);
        ImGui::Text("Storage Size: %.3f MiB", msg.storageSize);
        ImGui::Text("Bandwidth Percentage: %.3f", msg.bandwidthPercentage * 100.0);
        ImGui::Text("Time Since Last Update: %.3lf (s)", (long long)msg.timeSinceUpdate.count()/1000.0);
        float rowHeight = ImGui::GetTextLineHeightWithSpacing() + 4.0;
        ImVec2 listSize(ImGui::GetContentRegionAvail().x, rowHeight * msg.signals.size() + ImGui::GetStyle().FramePadding.y);
        ImGui::Separator();
        if(ImGui::BeginListBox("##popupList", listSize)){
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                for(size_t idx = 0; idx < msg.signals.size(); ++idx){
                    bool isSelected = (idx == selected);
                    if (ImGui::Selectable(msg.signals[idx].name.c_str(), isSelected, ImGuiSelectableFlags_SelectOnNav))
                        selected = (int)idx;
                    if (ImGui::IsItemFocused()) selected = static_cast<int>(idx);
                    if (isSelected){
                        ImGui::SetItemDefaultFocus();
                        const float groupWidth = popupWindowSize.x + gap + popupWideSize.x;
                        ImVec2 groupOrigin(center.x - groupWidth * 0.5f, center.y);
                        ImVec2 popupWidePos(groupOrigin.x + popupWindowSize.x + gap,
                                            center.y - popupWideSize.y * 0.5f);
                        popupWidePos = clampPos(popupWidePos, popupWideSize);
                        childFocused = popupWide(msg.signals[idx], msg.time, popupWidePos, &popupWideSize);
                    }
                }
            ImGui::EndListBox();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    return (focused || childFocused);
}

bool UI::popupWide(const CanSignal& sig, const std::vector<double>& time, ImVec2 pos, ImVec2* outSize){
    bool focused = false;
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | 
                             ImGuiWindowFlags_NoSavedSettings  | 
                             ImGuiWindowFlags_NoTitleBar | 
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
    ImGui::SetNextWindowBgAlpha(0.50);
    ImGui::SetNextWindowPos(pos);
    if(ImGui::Begin((sig.name + "wide##").data(), NULL, flags)){
        focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGui::Text("Name: %s", sig.name.c_str());
        ImGui::SameLine();
        ImGui::Text("Start Bit: %d", sig.startBit);
        ImGui::SameLine();
        ImGui::Text("Length: %d", sig.length);
        ImGui::SameLine();
        ImGui::Text("Endianness: %d", sig.endianness);
        ImGui::SameLine();
        ImGui::Text("Signed: %s", sig.isSigned ? "true" : "false");
        ImGui::SameLine();
        ImGui::Text("Scale: %.3f", sig.scale);
        ImGui::SameLine();
        ImGui::Text("Offset: %.3f", sig.offset);

        ImGui::Text("Min: %.3f", sig.min);
        ImGui::SameLine();
        ImGui::Text("Max: %.3f", sig.max);
        ImGui::SameLine();
        ImGui::Text("Unit: %s", sig.unit.c_str());
        ImGui::SameLine();
        ImGui::Text("Receiver: %s", sig.receiver.c_str());
        ImGui::SameLine();
        ImGui::Text("Last Mutated: %.3f s ago",
        std::chrono::duration<double>(std::chrono::system_clock::now() - sig.lastTimeMutated).count());
        ImGui::SameLine();
        ImGui::Text("Last: %.3f", sig.data.back());

        genericInlinePlot(time, sig.data, sig.name.c_str());
        if (outSize != nullptr) {
            *outSize = ImGui::GetWindowSize();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    return focused;
}

int levenshtein(const std::string& a, const std::string& b) {
    const int n = a.size();
    const int m = b.size();

    std::vector<int> prev(m+1), cur(m+1);
    for (int j = 0; j <= m; j++) prev[j] = j;
    for (int i = 1; i <= n; i++) {
        cur[0] = i;
        for(int j = 1; j <= m; j++){
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            cur[j] = std::min({
                prev[j] + 1,
                cur[j-1] + 1,
                prev[j-1] + cost
            });
        }
        prev.swap(cur);
    }
    return prev[m];
}

int distance(std::string a, std::string b) {
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);

    if (a.size() >= b.size())
        return levenshtein(a, b);

    int best = INT_MAX;
    for (size_t i = 0; i + a.size() <= b.size(); i++) {
        int d = levenshtein(a, b.substr(i, a.size()));
        if (d < best) best = d;
    }
    return best;
}

void UI::search(){
    cmdResults.clear();
    if (cmdBuffer[0] == '\0') {
        cmdSelected = -1;
        return;
    }

    std::vector<CmdResult> results;
    results.reserve(parseINTF->canStore.canMessages.size());

    for (auto& [id, msg] : parseINTF->canStore.canMessages) {
        int d = distance(cmdBuffer, msg.name);
        results.emplace_back(msg.name, d, msg.canId);
    }

    if (results.empty()) {
        cmdSelected = -1;
        return;
    }

    std::sort(
        results.begin(),
        results.end(),
        [](const CmdResult& a, const CmdResult& b) {
            if (a.distance != b.distance) return a.distance < b.distance;
            return a.name < b.name;
        });

    cmdResults = std::move(results);

    if (cmdSelected >= static_cast<int>(cmdResults.size())) {
        cmdSelected = cmdResults.empty() ? -1 : 0;
    } else if (cmdSelected == -1 && !cmdResults.empty()) {
        cmdSelected = 0;
    }
}

void UI::shaderWindow(VulkanShader& shader, std::string windowName){
    ImGui::SetNextWindowSize(ImVec2(shader.extent.width, shader.extent.height), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration;
    if(ImGui::Begin(windowName.data(), NULL, flags)){
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        if(contentSize.x <= 1.0f || contentSize.y <= 1.0f){
            contentSize = ImVec2(shader.extent.width, shader.extent.height);
        }
        const float delta = 0.5f;
        if(contentSize.x > 1.0f && contentSize.y > 1.0f){
            if(std::fabs(contentSize.x - shader.extent.width) > delta ||
               std::fabs(contentSize.y - shader.extent.height) > delta){
                shader.extent.width = contentSize.x;
                shader.extent.height = contentSize.y;
                shader.dirty = true;
            }
        }
        ImVec2 drawSize(shader.extent.width, shader.extent.height);
        drawSize.x = std::max(drawSize.x, 1.0f);
        drawSize.y = std::max(drawSize.y, 1.0f);
        ImGui::Image(shader.texture, drawSize);
    }
    ImGui::End();
}

void UI::objWindow(VulkanObj& obj, std::string name){
    ImGui::SetNextWindowSize(ImVec2(obj.extent.width, obj.extent.height), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration;
    if(ImGui::Begin(name.data(), NULL, flags)){
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        if(contentSize.x <= 1.0f || contentSize.y <= 1.0f){
            contentSize = ImVec2(obj.extent.width, obj.extent.height);
        }
        const float delta = 0.5f;
        if(contentSize.x > 1.0f && contentSize.y > 1.0f){
            if(std::fabs(contentSize.x - obj.extent.width) > delta ||
               std::fabs(contentSize.y - obj.extent.height) > delta){
                obj.extent.width = contentSize.x;
                obj.extent.height = contentSize.y;
                obj.dirty = true;
            }
        }
        ImVec2 drawSize(obj.extent.width, obj.extent.height);
        drawSize.x = std::max(drawSize.x, 1.0f);
        drawSize.y = std::max(drawSize.y, 1.0f);
        ImGui::Image(obj.outTexture, drawSize);
    } ImGui::End();
}

void UI::showVideoDisplay(){
    if (!videoSource.texture) { return; }
    if (ImGui::Begin("Custom Image", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImVec2 size = ImVec2(videoSource.textureSize.width, videoSource.textureSize.height);
        if (size.x <= 0.0f || size.y <= 0.0f) { size = ImVec2(512.0f, 512.0f); }
        ImVec2 available = ImGui::GetContentRegionAvail();
        ImVec2 drawSize = size;
        if (available.x > 0.0f && available.y > 0.0f) {
            float scaleX = available.x / size.x;
            float scaleY = available.y / size.y;
            float scale = scaleX < scaleY ? scaleX : scaleY;
            if (scale < 1.0f) {
                drawSize.x = size.x * scale;
                drawSize.y = size.y * scale;
            }
        }
        ImGui::Image(videoSource.texture, drawSize);
    }
    ImGui::End();
}

void UI::background(){
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;
    if (displaySize.x > 0.0f && displaySize.y > 0.0f) {
        const float epsilon = 0.5f;
        if (std::fabs(backgroundShader.extent.width- displaySize.x) > epsilon ||
            std::fabs(backgroundShader.extent.height - displaySize.y) > epsilon) {
            backgroundShader.extent.width = displaySize.x;
            backgroundShader.extent.height = displaySize.y;
            backgroundShader.dirty = true;
        }
    }

    if (!backgroundShader.texture) { return; }

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    if (!viewport) { return; }

    ImDrawList *drawList = ImGui::GetBackgroundDrawList(viewport);
    ImVec2 min = viewport->Pos;
    ImVec2 max = ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
    drawList->AddImage(this->backgroundShader.texture, min, max);
    static float alpha = 80;
    drawList->AddRectFilled(min, max, IM_COL32(0,0,0,alpha));
}

void UI::setStyle(){
    ImGuiStyle &UIstyle = ImGui::GetStyle();
    ImVec4* colors = UIstyle.Colors;

    UIstyle.WindowRounding = 01.0f;
    UIstyle.ChildRounding = 12.0f;
    UIstyle.PopupRounding = 12.0f;
    UIstyle.PopupBorderSize = UIstyle.WindowBorderSize;
    UIstyle.PopupRounding = UIstyle.WindowRounding;

    colors[ImGuiCol_WindowBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 0.5f); 
    colors[ImGuiCol_ChildBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
    colors[ImGuiCol_PopupBg] = 
        ImVec4(0.02f, 0.02f, 0.02f, 0.95f);

    // Borders and separators
    colors[ImGuiCol_Border] =
        ImVec4(0.2f, 0.2f, 0.2f, 1.0f); 
    colors[ImGuiCol_Separator] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);

    // Text colors
    colors[ImGuiCol_Text] = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); 
    colors[ImGuiCol_TextDisabled] =
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f); 

    // Headers and title
    colors[ImGuiCol_Header] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f); 
    colors[ImGuiCol_HeaderHovered] =
        ImVec4(0.25f, 0.25f, 0.25f, 1.0f); 
    colors[ImGuiCol_HeaderActive] =
        ImVec4(0.3f, 0.3f, 0.3f, 1.0f); 

    colors[ImGuiCol_TitleBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.7f);

    // Buttons
    colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

    // Sliders, checks, etc.
    colors[ImGuiCol_SliderGrab] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

    colors[ImGuiCol_CheckMark] =
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

    // Frame backgrounds
    colors[ImGuiCol_FrameBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2f, 0.2f, 0.2f, 0.9f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.1f, 0.1f, 0.1f, 0.8f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);

    // Resize grips
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.2f, 0.2f, 0.2f, 0.2f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.5f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.4f, 0.4f, 0.4f, 0.7f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.2f, 0.2f, 0.2f, 0.7f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.8f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

    // Misc
    colors[ImGuiCol_PlotLines] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

    colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 1.0f, 0.9f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

    // Transparency handling
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);

    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    UIstyle.ScaleAllSizes(1.0f);
    ImPlotStyle &plotStyle = ImPlot::GetStyle();
    // Transparent plot backgrounds.
    plotStyle.Colors[ImPlotCol_FrameBg] = ImVec4(0, 0, 0, 0.0f);
    plotStyle.Colors[ImPlotCol_PlotBg] = ImVec4(0, 0, 0, 0.0f);
    plotStyle.Colors[ImPlotCol_PlotBorder] = ImVec4(0, 0, 0, 0.0f);
    plotStyle.Colors[ImPlotCol_LegendBg] = ImVec4(0, 0, 0, 0.0f);
    plotStyle.Colors[ImPlotCol_LegendBorder] = ImVec4(0, 0, 0, 0.0f);
}
