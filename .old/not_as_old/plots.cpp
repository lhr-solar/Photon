#include "plots.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include "../parse/parse.hpp"

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
    {"3D Line Plots", 2, 3, false, true},
    {"3D Scatter Plots", 2, 3, false, true},
    {"3D Surface Plots", 2, 3, false, true},
    {"List", 1, 128, false, false},
}};

constexpr size_t kMaxRenderablePoints = 4096;
constexpr size_t kMaxRenderableScatterPoints = 512;
constexpr size_t kMaxRenderableHeatmapCells = 1024;
constexpr size_t kMaxRenderable3DLinePoints = 1024;
constexpr size_t kMaxRenderable3DScatterPoints = 256;
constexpr int kMaxSurfaceSide = 24;

struct RenderSlice{
    size_t start = 0;
    size_t step = 1;
    int count = 0;
};

RenderSlice makeRenderSlice(size_t start, size_t end, size_t maxPoints = kMaxRenderablePoints){
    RenderSlice slice{};
    slice.start = start;
    if(end <= start) return slice;
    const size_t total = end - start;
    slice.step = std::max<size_t>(1, (total + maxPoints - 1) / maxPoints);
    slice.count = static_cast<int>((total + slice.step - 1) / slice.step);
    return slice;
}

int required3DSources(bool useSource1TimeAsX){
    return useSource1TimeAsX ? 2 : 3;
}

const char* threeDSourceLabel(size_t sourceIndex, bool useSource1TimeAsX){
    if(useSource1TimeAsX) return sourceIndex == 0 ? "Y Source" : "Z Source";
    if(sourceIndex == 0) return "X Source";
    if(sourceIndex == 1) return "Y Source";
    return "Z Source";
}

std::string sourceSlotLabel(int typeIndex, size_t sourceIndex, bool useSource1TimeAsX){
    if(kPlotSpecs[static_cast<size_t>(typeIndex)].is3D) return threeDSourceLabel(sourceIndex, useSource1TimeAsX);
    char label[32]{};
    std::snprintf(label, sizeof(label), "Signal %zu", sourceIndex + 1);
    return label;
}

Message* findMessage(Parse* parse, uint32_t messageId){
    if(!parse || messageId >= parse->arena.messages.size()) return nullptr;
    return parse->arena.messages[messageId];
}

Signal* findSignal(Parse* parse, const PlotManager::PlotSourceRef& ref){
    if(!ref.assigned) return nullptr;
    Message* msg = findMessage(parse, ref.messageId);
    if(!msg || ref.signalIndex >= msg->signalCount) return nullptr;
    return msg->signals[ref.signalIndex];
}

bool readSource(Parse* parse, const PlotManager::PlotSourceRef& ref, const double*& times, const double*& values, int& count){
    times = nullptr;
    values = nullptr;
    count = 0;
    if(!ref.assigned || !parse) return false;

    void* timeData = nullptr;
    void* signalData = nullptr;
    uint32_t timeBytes = 0;
    uint32_t signalBytes = 0;
    parse->arena.readTime(ref.messageId, &timeData, &timeBytes);
    parse->arena.read(ref.messageId, ref.signalIndex, &signalData, &signalBytes);
    const uint32_t bytes = std::min(timeBytes, signalBytes);
    const int samples = static_cast<int>(bytes / sizeof(double));
    if(samples <= 0 || !timeData || !signalData) return false;
    times = static_cast<const double*>(timeData);
    values = static_cast<const double*>(signalData);
    count = samples;
    return true;
}

double paddedMin(double minValue, double maxValue){
    if(std::abs(maxValue - minValue) < 1e-9){
        const double span = std::max(1.0, std::abs(minValue));
        return minValue - span * 0.5;
    }
    return minValue - ((maxValue - minValue) * 0.1);
}

double paddedMax(double minValue, double maxValue){
    if(std::abs(maxValue - minValue) < 1e-9){
        const double span = std::max(1.0, std::abs(maxValue));
        return maxValue + span * 0.5;
    }
    return maxValue + ((maxValue - minValue) * 0.1);
}

bool hasAllSources(const std::vector<PlotManager::PlotSourceRef>& sources){
    if(sources.empty()) return false;
    for(const auto& source : sources)
        if(!source.assigned) return false;
    return true;
}

void updateFollowState(PlotManager::PlotWindow& plot){
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
    if(recenterToLive){
        plot.followLatest = true;
        plot.hasView = false;
    }
    if(isNavigating){
        plot.followLatest = false;
    }

    const ImPlotRect limits = ImPlot::GetPlotLimits(ImAxis_X1, ImAxis_Y1);
    plot.xMin = limits.X.Min;
    plot.xMax = limits.X.Max;
    plot.hasView = true;
}

std::string signalDisplayName(Parse* parse, const PlotManager::PlotSourceRef& ref){
    Message* msg = findMessage(parse, ref.messageId);
    Signal* sig = findSignal(parse, ref);
    if(!msg || !sig) return "<unassigned>";
    char label[256]{};
    std::snprintf(label, sizeof(label), "0x%03X / %s", msg->id, sig->name.c_str());
    return label;
}

bool setupTimeSeriesPlot(Parse* parse, PlotManager::PlotWindow& plot, const char*& overlayText){
    overlayText = nullptr;
    if(plot.sources.empty()){
        ImPlot::SetNextAxisLimits(ImAxis_X1, 0.0, 1.0, ImGuiCond_Always);
        ImPlot::SetNextAxisLimits(ImAxis_Y1, 0.0, 1.0, ImGuiCond_Always);
        overlayText = "Missing data sources.";
        return false;
    }

    const double* primaryTimes = nullptr;
    const double* primaryValues = nullptr;
    int primaryCount = 0;
    if(!readSource(parse, plot.sources[0], primaryTimes, primaryValues, primaryCount) || primaryCount < 2){
        ImPlot::SetNextAxisLimits(ImAxis_X1, 0.0, 1.0, ImGuiCond_Always);
        ImPlot::SetNextAxisLimits(ImAxis_Y1, 0.0, 1.0, ImGuiCond_Always);
        overlayText = "Need live samples for the selected sources.";
        return false;
    }

    constexpr double maxTime = 5.0;
    const double dataStart = primaryTimes[0];
    const double latestTime = primaryTimes[primaryCount - 1];
    const double liveWindowStart = std::max(dataStart, latestTime - maxTime);
    double rangeStart = liveWindowStart;
    double rangeEnd = latestTime;
    if(!plot.followLatest && plot.hasView){
        rangeStart = std::max(dataStart, plot.xMin);
        rangeEnd = std::max(rangeStart, plot.xMax);
        if(rangeEnd > latestTime){
            const double span = rangeEnd - rangeStart;
            rangeEnd = latestTime;
            rangeStart = std::max(dataStart, rangeEnd - span);
        }
    }

    auto minIt = std::lower_bound(primaryTimes, primaryTimes + primaryCount, rangeStart);
    auto maxIt = std::upper_bound(primaryTimes, primaryTimes + primaryCount, rangeEnd);
    if(minIt == primaryTimes + primaryCount || minIt >= maxIt){
        minIt = primaryTimes;
        maxIt = primaryTimes + primaryCount;
    }
    const size_t startIdx = static_cast<size_t>(minIt - primaryTimes);
    const size_t endIdx = static_cast<size_t>(maxIt - primaryTimes);
    if(startIdx >= endIdx){
        ImPlot::SetNextAxisLimits(ImAxis_X1, 0.0, 1.0, ImGuiCond_Always);
        ImPlot::SetNextAxisLimits(ImAxis_Y1, 0.0, 1.0, ImGuiCond_Always);
        overlayText = "Not enough data in the visible range.";
        return false;
    }

    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
    bool hasY = false;
    for(const auto& source : plot.sources){
        const double* timeValues = nullptr;
        const double* signalValues = nullptr;
        int count = 0;
        if(!readSource(parse, source, timeValues, signalValues, count) || count < 2) continue;
        const size_t usableEnd = std::min(endIdx, static_cast<size_t>(count));
        if(startIdx >= usableEnd) continue;
        for(size_t i = startIdx; i < usableEnd; ++i){
            yMin = std::min(yMin, signalValues[i]);
            yMax = std::max(yMax, signalValues[i]);
            hasY = true;
        }
    }
    if(!hasY){
        ImPlot::SetNextAxisLimits(ImAxis_X1, 0.0, 1.0, ImGuiCond_Always);
        ImPlot::SetNextAxisLimits(ImAxis_Y1, 0.0, 1.0, ImGuiCond_Always);
        overlayText = "Selected sources have no aligned points yet.";
        return false;
    }

    if(plot.followLatest){
        ImPlot::SetNextAxisLimits(ImAxis_X1, liveWindowStart, latestTime, ImGuiCond_Always);
    } else{
        plot.xMin = rangeStart;
        plot.xMax = rangeEnd;
        ImPlot::SetNextAxisLinks(ImAxis_X1, &plot.xMin, &plot.xMax);
    }
    ImPlot::SetNextAxisLimits(ImAxis_Y1, paddedMin(yMin, yMax), paddedMax(yMin, yMax), ImGuiCond_Always);
    return true;
}

void drawTimeSeriesPlot(Parse* parse, PlotManager::PlotWindow& plot){
    if(plot.sources.empty()) return;

    const double* primaryTimes = nullptr;
    const double* primaryValues = nullptr;
    int primaryCount = 0;
    if(!readSource(parse, plot.sources[0], primaryTimes, primaryValues, primaryCount) || primaryCount < 2) return;

    constexpr double maxTime = 5.0;
    const double dataStart = primaryTimes[0];
    const double latestTime = primaryTimes[primaryCount - 1];
    const double liveWindowStart = std::max(dataStart, latestTime - maxTime);
    double rangeStart = plot.followLatest ? liveWindowStart : std::max(dataStart, plot.xMin);
    double rangeEnd = plot.followLatest ? latestTime : std::max(rangeStart, plot.xMax);
    if(rangeEnd > latestTime){
        const double span = rangeEnd - rangeStart;
        rangeEnd = latestTime;
        rangeStart = std::max(dataStart, rangeEnd - span);
    }

    auto minIt = std::lower_bound(primaryTimes, primaryTimes + primaryCount, rangeStart);
    auto maxIt = std::upper_bound(primaryTimes, primaryTimes + primaryCount, rangeEnd);
    if(minIt == primaryTimes + primaryCount || minIt >= maxIt){
        minIt = primaryTimes;
        maxIt = primaryTimes + primaryCount;
    }
    const size_t startIdx = static_cast<size_t>(minIt - primaryTimes);
    const size_t endIdx = static_cast<size_t>(maxIt - primaryTimes);
    if(startIdx >= endIdx) return;

    for(size_t i = 0; i < plot.sources.size(); ++i){
        const auto& source = plot.sources[i];
        const double* timeValues = nullptr;
        const double* signalValues = nullptr;
        int count = 0;
        if(!readSource(parse, source, timeValues, signalValues, count) || count < 2) continue;
        Signal* sig = findSignal(parse, source);
        if(!sig) continue;
        const size_t usableEnd = std::min(endIdx, static_cast<size_t>(count));
        if(usableEnd <= startIdx) continue;
        const RenderSlice slice = makeRenderSlice(startIdx, usableEnd,
            plot.typeIndex == PlotType_Scatter ? kMaxRenderableScatterPoints : kMaxRenderablePoints);
        if(slice.count < 2) continue;

        const char* label = sig->name.c_str();
        const int stride = static_cast<int>(sizeof(double) * slice.step);
        const ImPlotSpec strideSpec(ImPlotProp_Stride, stride);
        switch(plot.typeIndex){
            case PlotType_Line:
                ImPlot::PlotLine(label, primaryTimes + slice.start, signalValues + slice.start, slice.count, strideSpec);
                break;
            case PlotType_FilledLine:
                ImPlot::PlotShaded(label, primaryTimes + slice.start, signalValues + slice.start, slice.count, 0.0, strideSpec);
                ImPlot::PlotLine(label, primaryTimes + slice.start, signalValues + slice.start, slice.count, strideSpec);
                break;
            case PlotType_Scatter:
                ImPlot::PlotScatter(label, primaryTimes + slice.start, signalValues + slice.start, slice.count, strideSpec);
                break;
            case PlotType_Stairstep:
                ImPlot::PlotStairs(label, primaryTimes + slice.start, signalValues + slice.start, slice.count, strideSpec);
                break;
            case PlotType_Bar:
                ImPlot::PlotBars(label, primaryTimes + slice.start, signalValues + slice.start, slice.count, 0.05, strideSpec);
                break;
            case PlotType_Stem:
                ImPlot::PlotStems(label, primaryTimes + slice.start, signalValues + slice.start, slice.count, 0.0, strideSpec);
                break;
            case PlotType_Digital:
                ImPlot::PlotDigital(label, primaryTimes + slice.start, signalValues + slice.start, slice.count, strideSpec);
                break;
            default:
                break;
        }
    }

    if(plot.typeIndex == PlotType_Shaded && plot.sources.size() >= 2){
        const double* t0 = nullptr; const double* y0 = nullptr; int c0 = 0;
        const double* t1 = nullptr; const double* y1 = nullptr; int c1 = 0;
        if(readSource(parse, plot.sources[0], t0, y0, c0) && readSource(parse, plot.sources[1], t1, y1, c1)){
            const size_t usableEnd = std::min({endIdx, static_cast<size_t>(c0), static_cast<size_t>(c1)});
            const RenderSlice slice = makeRenderSlice(startIdx, usableEnd);
            if(slice.count >= 2){
                ImPlot::PlotShaded("Shaded", primaryTimes + slice.start, y0 + slice.start, y1 + slice.start,
                    slice.count, ImPlotSpec(ImPlotProp_Stride, static_cast<int>(sizeof(double) * slice.step)));
            }
        }
    }

    if(plot.typeIndex == PlotType_ErrorBars && plot.sources.size() >= 2){
        const double* t0 = nullptr; const double* y0 = nullptr; int c0 = 0;
        const double* t1 = nullptr; const double* y1 = nullptr; int c1 = 0;
        if(readSource(parse, plot.sources[0], t0, y0, c0) && readSource(parse, plot.sources[1], t1, y1, c1)){
            const size_t usableEnd = std::min({endIdx, static_cast<size_t>(c0), static_cast<size_t>(c1)});
            const RenderSlice slice = makeRenderSlice(startIdx, usableEnd);
            if(slice.count >= 2){
                Signal* base = findSignal(parse, plot.sources[0]);
                if(base){
                    const ImPlotSpec strideSpec(ImPlotProp_Stride, static_cast<int>(sizeof(double) * slice.step));
                    ImPlot::PlotLine(base->name.c_str(), primaryTimes + slice.start, y0 + slice.start, slice.count, strideSpec);
                    ImPlot::PlotErrorBars("Error", primaryTimes + slice.start, y0 + slice.start, y1 + slice.start,
                        slice.count, strideSpec);
                }
            }
        }
    }

    if((plot.typeIndex == PlotType_BarGroups || plot.typeIndex == PlotType_BarStacks) && plot.sources.size() >= 2){
        std::vector<const double*> series{};
        std::vector<int> seriesCounts{};
        for(const auto& source : plot.sources){
            const double* t = nullptr; const double* y = nullptr; int c = 0;
            if(readSource(parse, source, t, y, c) && c > 0){
                series.push_back(y);
                seriesCounts.push_back(c);
            }
        }
        if(series.size() < 2) {
            updateFollowState(plot);
            return;
        }
        const int itemCount = static_cast<int>(series.size());
        const int groupCount = std::min(64, static_cast<int>(makeRenderSlice(startIdx, std::min(endIdx, static_cast<size_t>(*std::min_element(seriesCounts.begin(), seriesCounts.end()))), 512).count));
        if(groupCount > 0){
            std::vector<double> values(static_cast<size_t>(itemCount * groupCount), 0.0);
            std::vector<const char*> labels(static_cast<size_t>(itemCount), nullptr);
            const RenderSlice slice = makeRenderSlice(startIdx, endIdx, 512);
            for(int item = 0; item < itemCount; ++item){
                Signal* sig = findSignal(parse, plot.sources[static_cast<size_t>(item)]);
                labels[static_cast<size_t>(item)] = sig ? sig->name.c_str() : "?";
                for(int group = 0; group < groupCount; ++group)
                    values[static_cast<size_t>(item * groupCount + group)] = series[static_cast<size_t>(item)][slice.start + static_cast<size_t>(group) * slice.step];
            }
            const ImPlotBarGroupsFlags flags = plot.typeIndex == PlotType_BarStacks ? ImPlotBarGroupsFlags_Stacked : ImPlotBarGroupsFlags_None;
            ImPlot::PlotBarGroups(labels.data(), values.data(), itemCount, groupCount, 0.67, 0.0,
                ImPlotSpec(ImPlotProp_Flags, flags));
        }
    }

    updateFollowState(plot);
}

void renderNonTimePlot(Parse* parse, const PlotManager::PlotWindow& plot){
    std::vector<const double*> ys{};
    std::vector<int> counts{};
    ys.reserve(plot.sources.size());
    counts.reserve(plot.sources.size());

    for(const auto& source : plot.sources){
        const double* timeValues = nullptr;
        const double* signalValues = nullptr;
        int count = 0;
        if(!readSource(parse, source, timeValues, signalValues, count) || count <= 0) continue;
        ys.push_back(signalValues);
        counts.push_back(count);
    }
    if(ys.empty()){
        ImGui::TextUnformatted("Need live samples for the selected sources.");
        return;
    }

    if(plot.typeIndex == PlotType_List){
        if(ImGui::BeginTable("##plot_list", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)){
            ImGui::TableSetupColumn("Signal");
            ImGui::TableSetupColumn("Latest");
            ImGui::TableHeadersRow();
            for(const auto& source : plot.sources){
                const double* timeValues = nullptr;
                const double* signalValues = nullptr;
                int count = 0;
                Signal* sig = findSignal(parse, source);
                if(!sig || !readSource(parse, source, timeValues, signalValues, count) || count <= 0) continue;
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

    if(plot.typeIndex == PlotType_Pie){
        std::vector<double> values(ys.size(), 0.0);
        std::vector<const char*> labels(ys.size(), nullptr);
        for(size_t i = 0; i < ys.size(); ++i){
            Signal* sig = findSignal(parse, plot.sources[i]);
            values[i] = ys[i][counts[i] - 1];
            labels[i] = sig ? sig->name.c_str() : "?";
        }
        ImPlot::PlotPieChart(labels.data(), values.data(), static_cast<int>(values.size()), 0.5, 0.5, 0.4, "%.2f");
        return;
    }

    if(plot.typeIndex == PlotType_Heatmap){
        const int count = counts[0];
        if(count <= 0) return;
        const int usable = std::min(count, static_cast<int>(kMaxRenderableHeatmapCells));
        const int cols = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(usable))));
        const int rows = std::max(1, usable / cols);
        if(rows * cols > 0) ImPlot::PlotHeatmap("Heatmap", ys[0] + (count - (rows * cols)), rows, cols, 0.0, 0.0, "");
        return;
    }

    if(plot.typeIndex == PlotType_Histogram){
        for(size_t i = 0; i < ys.size(); ++i){
            Signal* sig = findSignal(parse, plot.sources[i]);
            if(sig && counts[i] > 0) ImPlot::PlotHistogram(sig->name.c_str(), ys[i], counts[i]);
        }
        return;
    }

    if(plot.typeIndex == PlotType_Histogram2D && ys.size() >= 2){
        const int count = std::min(counts[0], counts[1]);
        if(count > 1) ImPlot::PlotHistogram2D("Histogram2D", ys[0], ys[1], count);
    }
}

void render3DPlot(Parse* parse, const PlotManager::PlotWindow& plot){
    if(plot.sources.size() < static_cast<size_t>(required3DSources(plot.useSource1TimeAsX))){
        ImGui::TextUnformatted("Missing sources for 3D plot.");
        return;
    }

    const double* x = nullptr;
    const double* y = nullptr;
    const double* z = nullptr;
    int xCount = 0;
    int yCount = 0;
    int zCount = 0;
    const double* tmpTime = nullptr;

    if(plot.useSource1TimeAsX){
        if(!readSource(parse, plot.sources[0], x, y, yCount)) return;
        tmpTime = x;
        if(!readSource(parse, plot.sources[1], x, z, zCount)) return;
        x = tmpTime;
        xCount = yCount;
    } else{
        if(!readSource(parse, plot.sources[0], tmpTime, x, xCount)) return;
        if(!readSource(parse, plot.sources[1], tmpTime, y, yCount)) return;
        if(!readSource(parse, plot.sources[2], tmpTime, z, zCount)) return;
    }

    const int count = plot.useSource1TimeAsX ? std::min(yCount, zCount) : std::min({xCount, yCount, zCount});
    if(count < 2){
        ImGui::TextUnformatted("Need live samples for the selected sources.");
        return;
    }

    Signal* s0 = findSignal(parse, plot.sources[0]);
    Signal* s1 = findSignal(parse, plot.sources[1]);
    Signal* s2 = plot.useSource1TimeAsX ? nullptr : findSignal(parse, plot.sources[2]);
    ImPlot3D::SetupAxes(plot.useSource1TimeAsX ? "time" : (s0 ? s0->name.c_str() : "x"),
        plot.useSource1TimeAsX ? (s0 ? s0->name.c_str() : "y") : (s1 ? s1->name.c_str() : "y"),
        plot.useSource1TimeAsX ? (s1 ? s1->name.c_str() : "z") : (s2 ? s2->name.c_str() : "z"));

    if(plot.typeIndex == PlotType_3DLine){
        const RenderSlice slice = makeRenderSlice(0, static_cast<size_t>(count), kMaxRenderable3DLinePoints);
        if(slice.count >= 2) ImPlot3D::PlotLine("3D Line", x + slice.start, y + slice.start, z + slice.start,
            slice.count, ImPlot3DSpec(ImPlot3DProp_Stride, static_cast<int>(sizeof(double) * slice.step)));
        return;
    }
    if(plot.typeIndex == PlotType_3DScatter){
        const RenderSlice slice = makeRenderSlice(0, static_cast<size_t>(count), kMaxRenderable3DScatterPoints);
        if(slice.count >= 2) ImPlot3D::PlotScatter("3D Scatter", x + slice.start, y + slice.start, z + slice.start,
            slice.count, ImPlot3DSpec(ImPlot3DProp_Stride, static_cast<int>(sizeof(double) * slice.step)));
        return;
    }
    if(plot.typeIndex == PlotType_3DSurface){
        const int side = std::min(kMaxSurfaceSide, std::max(2, static_cast<int>(std::sqrt(static_cast<double>(count)))));
        const int pointCount = side * side;
        if(pointCount >= 4 && pointCount <= count){
            const int start = count - pointCount;
            ImPlot3D::PlotSurface("3D Surface", x + start, y + start, z + start, side, side, 0.0, 0.0,
                ImPlot3DSpec(ImPlot3DProp_Flags, ImPlot3DSurfaceFlags_NoLines));
        }
    }
}

void renderPlotBody(Parse* parse, PlotManager::PlotWindow& plot){
    char plotId[64]{};
    std::snprintf(plotId, sizeof(plotId), "##generated_plot_%d", plot.id);

    if(kPlotSpecs[static_cast<size_t>(plot.typeIndex)].is3D){
        if(ImPlot3D::BeginPlot(plotId, ImVec2(-1.0f, -1.0f))) {
            render3DPlot(parse, plot);
            ImPlot3D::EndPlot();
        }
        return;
    }

    if(plot.typeIndex == PlotType_List){
        renderNonTimePlot(parse, plot);
        return;
    }

    const char* overlayText = nullptr;
    if(kPlotSpecs[static_cast<size_t>(plot.typeIndex)].usesTimeAxis){
        setupTimeSeriesPlot(parse, plot, overlayText);
    }
    if(ImPlot::BeginPlot(plotId, ImVec2(-1.0f, -1.0f))){
        if(kPlotSpecs[static_cast<size_t>(plot.typeIndex)].usesTimeAxis){
            ImPlot::SetupAxes("time", nullptr, ImPlotAxisFlags_None, ImPlotAxisFlags_None);
            if(overlayText) ImPlot::PlotText(overlayText, 0.5, 0.5);
            else drawTimeSeriesPlot(parse, plot);
        } else{
            renderNonTimePlot(parse, plot);
        }
        ImPlot::EndPlot();
    }
}

bool matchQuery(const std::string& haystack, const char* query){
    if(!query || query[0] == '\0') return true;
    std::string lowerHaystack = haystack;
    std::string lowerQuery = query;
    std::transform(lowerHaystack.begin(), lowerHaystack.end(), lowerHaystack.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return lowerHaystack.find(lowerQuery) != std::string::npos;
}
}

const PlotManager::PlotTypeSpec& PlotManager::specFor(int index){
    return kPlotSpecs[static_cast<size_t>(std::clamp(index, 0, PlotType_Count - 1))];
}

void PlotManager::init(Parse* parseTarget){
    parse = parseTarget;
    typeIndex = PlotType_Line;
    useSource1TimeAsX = true;
    resetPendingSourcesForType();
    refreshSignalOptions();
    refreshMatches();
}

void PlotManager::handleHotkeys(bool homeActive){
    if(!homeActive) return;
    ImGuiIO& io = ImGui::GetIO();
    if(ImGui::IsKeyPressed(ImGuiKey_Slash, false) && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper && !io.WantTextInput){
        openCreator();
    }
}

void PlotManager::renderHome(ImGuiID dockspaceID, const ImVec2& contentMin, const ImVec2& contentMax){
    homeDockspaceID = dockspaceID;
    refreshSignalOptions();
    renderCreator();
    renderPlotWindows();
    if(windows.empty() && !creatorOpen){
        const char* hint = "Press \"/\" to create plots";
        const ImVec2 textSize = ImGui::CalcTextSize(hint);
        const float xCenter = (contentMin.x + contentMax.x) * 0.5f;
        const float yTopThird = contentMin.y + ((contentMax.y - contentMin.y) * 0.28f);
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->AddText(ImVec2(xCenter - textSize.x * 0.5f, yTopThird - textSize.y * 0.5f),
            ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.92f)), hint);
    }
}

void PlotManager::openCreator(){
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

void PlotManager::refreshSignalOptions(){
    signalOptions.clear();
    if(!parse) return;
    for(uint32_t messageId : parse->arena.validIds){
        Message* msg = findMessage(parse, messageId);
        if(!msg) continue;
        for(uint32_t signalIndex = 0; signalIndex < msg->signalCount; ++signalIndex){
            Signal* sig = msg->signals[signalIndex];
            if(!sig) continue;
            SignalOption option{};
            option.ref.messageId = messageId;
            option.ref.signalIndex = signalIndex;
            option.ref.assigned = true;
            char label[256]{};
            std::snprintf(label, sizeof(label), "0x%03X : %s / %s", messageId, msg->name.c_str(), sig->name.c_str());
            option.label = label;
            signalOptions.push_back(std::move(option));
        }
    }
}

void PlotManager::refreshMatches(){
    sourceMatches.clear();
    for(size_t i = 0; i < signalOptions.size(); ++i){
        if(matchQuery(signalOptions[i].label, search)) sourceMatches.push_back(static_cast<int>(i));
    }
    if(sourceMatches.empty()) selectedMatch = -1;
    else selectedMatch = std::clamp(selectedMatch, 0, static_cast<int>(sourceMatches.size()) - 1);
}

void PlotManager::resetPendingSourcesForType(){
    const PlotTypeSpec& spec = specFor(typeIndex);
    const int count = spec.is3D ? required3DSources(useSource1TimeAsX) : spec.minSources;
    pendingSources.assign(static_cast<size_t>(count), PlotSourceRef{});
    activeSourceIndex = std::clamp(activeSourceIndex, 0, std::max(0, count - 1));
}

void PlotManager::renderCreator(){
    if(!creatorOpen) return;

    const ImGuiStyle& style = ImGui::GetStyle();
    float maxSuggestionWidth = 0.0f;
    for(int optionIndex : sourceMatches){
        if(optionIndex < 0 || static_cast<size_t>(optionIndex) >= signalOptions.size()) continue;
        maxSuggestionWidth = std::max(maxSuggestionWidth, ImGui::CalcTextSize(signalOptions[static_cast<size_t>(optionIndex)].label.c_str()).x);
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float previewColumnWidth = 430.0f;
    const float assignedColumnWidth = 300.0f;
    const float minSearchColumnWidth = 320.0f;
    const float desiredSearchColumnWidth = std::max(
        minSearchColumnWidth,
        maxSuggestionWidth + style.FramePadding.x * 2.0f + style.CellPadding.x * 2.0f + style.ScrollbarSize + 24.0f);
    const float availableViewportWidth = viewport ? viewport->WorkSize.x : ImGui::GetIO().DisplaySize.x;
    const float maxWindowWidth = std::max(720.0f, availableViewportWidth - 48.0f);
    const float desiredWindowWidth = std::min(
        maxWindowWidth,
        previewColumnWidth + assignedColumnWidth + desiredSearchColumnWidth +
            style.WindowPadding.x * 2.0f + style.CellPadding.x * 6.0f + style.ItemSpacing.x * 2.0f);
    const ImVec2 center = viewport ? viewport->GetCenter() : ImVec2(0.0f, 0.0f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(desiredWindowWidth, 0.0f), ImGuiCond_Always);
    bool keepOpen = true;
    const ImGuiStyle& theme = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.Colors[ImGuiCol_PopupBg]);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.Colors[ImGuiCol_FrameBg]);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.Colors[ImGuiCol_Border]);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    if(!ImGui::Begin("Create Plot", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoDocking)){
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        creatorOpen = keepOpen;
        return;
    }

    if(ImGui::IsKeyPressed(ImGuiKey_Escape)) keepOpen = false;
    const PlotTypeSpec& spec = specFor(typeIndex);

    if(ImGui::BeginTable("##plot_creator", 3, ImGuiTableFlags_SizingFixedFit)){
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, previewColumnWidth);
        ImGui::TableSetupColumn("Assigned", ImGuiTableColumnFlags_WidthFixed, assignedColumnWidth);
        ImGui::TableSetupColumn("Search", ImGuiTableColumnFlags_WidthFixed, desiredSearchColumnWidth);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Plot Type");
        ImGui::SetNextItemWidth(-1.0f);
        if(ImGui::BeginCombo("##plot_type", spec.label)){
            for(int i = 0; i < PlotType_Count; ++i){
                const bool selected = i == typeIndex;
                if(ImGui::Selectable(specFor(i).label, selected)){
                    typeIndex = i;
                    useSource1TimeAsX = true;
                    resetPendingSourcesForType();
                }
                if(selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if(spec.is3D && ImGui::Checkbox("Use Source 1 Time as X", &useSource1TimeAsX)){
            resetPendingSourcesForType();
        }

        if(!spec.is3D && spec.maxSources > spec.minSources){
            if(ImGui::Button("Add Signal") && static_cast<int>(pendingSources.size()) < spec.maxSources){
                pendingSources.push_back({});
            }
            ImGui::SameLine();
            if(ImGui::Button("Remove Signal") && static_cast<int>(pendingSources.size()) > spec.minSources){
                pendingSources.pop_back();
                activeSourceIndex = std::clamp(activeSourceIndex, 0, std::max(0, static_cast<int>(pendingSources.size()) - 1));
            }
        }

        if(ImGui::BeginChild("##plot_preview", ImVec2(0.0f, 280.0f), true)){
            PlotWindow preview{};
            preview.id = -1;
            preview.typeIndex = typeIndex;
            preview.sources = pendingSources;
            preview.useSource1TimeAsX = useSource1TimeAsX;
            renderPlotBody(parse, preview);
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("Minimum Sources: %d", spec.is3D ? required3DSources(useSource1TimeAsX) : spec.minSources);
        ImGui::Text("Maximum Sources: %d", spec.is3D ? required3DSources(useSource1TimeAsX) : spec.maxSources);
        ImGui::SeparatorText("Assigned Sources");
        for(size_t i = 0; i < pendingSources.size(); ++i){
            const bool selected = static_cast<int>(i) == activeSourceIndex;
            const std::string slotLabel = sourceSlotLabel(typeIndex, i, useSource1TimeAsX) + ": " + signalDisplayName(parse, pendingSources[i]);
            if(ImGui::Selectable(slotLabel.c_str(), selected)) activeSourceIndex = static_cast<int>(i);
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("Search");
        if(creatorFocusSearch){
            ImGui::SetKeyboardFocusHere();
            creatorFocusSearch = false;
        }
        if(ImGui::InputTextWithHint("##plot_search", "Search message, id, or signal", search, sizeof(search))){
            refreshMatches();
        }
        if(ImGui::BeginChild("##plot_search_results", ImVec2(0.0f, 280.0f), true, ImGuiWindowFlags_HorizontalScrollbar)){
            for(size_t i = 0; i < sourceMatches.size(); ++i){
                const int optionIndex = sourceMatches[i];
                const bool selected = static_cast<int>(i) == selectedMatch;
                if(ImGui::Selectable(signalOptions[static_cast<size_t>(optionIndex)].label.c_str(), selected,
                        ImGuiSelectableFlags_AllowDoubleClick,
                        ImVec2(maxSuggestionWidth + style.FramePadding.x * 2.0f, 0.0f))){
                    selectedMatch = static_cast<int>(i);
                    pendingSources[static_cast<size_t>(activeSourceIndex)] = signalOptions[static_cast<size_t>(optionIndex)].ref;
                    if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && activeSourceIndex + 1 < static_cast<int>(pendingSources.size()))
                        activeSourceIndex += 1;
                }
            }
        }
        ImGui::EndChild();

        ImGui::EndTable();
    }

    const bool canCreate = hasAllSources(pendingSources);
    if(!canCreate) ImGui::BeginDisabled();
    if(ImGui::Button("Create")){
        createPlot();
        keepOpen = false;
    }
    if(!canCreate) ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Cancel")) keepOpen = false;

    creatorOpen = keepOpen;
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

void PlotManager::renderPlotWindows(){
    windows.erase(std::remove_if(windows.begin(), windows.end(), [](const PlotWindow& window){ return !window.open; }), windows.end());
    for(PlotWindow& window : windows){
        if(homeDockspaceID != 0) ImGui::SetNextWindowDockID(homeDockspaceID, ImGuiCond_FirstUseEver);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
        if(ImGui::Begin(window.title.c_str(), &window.open, flags)){
            renderPlotBody(parse, window);
        }
        ImGui::End();
    }
}

void PlotManager::createPlot(){
    if(!hasAllSources(pendingSources)) return;
    PlotWindow window{};
    window.id = nextPlotId++;
    window.typeIndex = typeIndex;
    window.sources = pendingSources;
    window.useSource1TimeAsX = useSource1TimeAsX;
    window.title = std::string(specFor(typeIndex).label) + " " + std::to_string(window.id);
    windows.push_back(std::move(window));
}
