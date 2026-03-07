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
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include "console.hpp"
#include "imgui_internal.h"
#include "implot.h"
#include "implot3d.h"
#include "imgui_node_editor.h"

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
constexpr size_t kMaxRenderablePointsScatter2D = 512;
constexpr size_t kMaxRenderableHeatmapCells = 1024;
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
    PlotType_List,
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
    {"3D Line Plots", 2, 3, false, true},
    {"3D Scatter Plots", 3, 3, false, true},
    {"3D Surface Plots", 3, 3, false, true},
    {"List", 1, 1024, false, false}
}};

struct GeneratedPlotWindow {
    int id = 0;
    int typeIndex = PlotType_Line;
    std::vector<PlotDataSourceRef> sources;
    bool open = true;
    bool needsInitialDock = true;
    ImGuiID initialDockNode = 0;
    bool forceInitialDock = false;
    bool undockedInteracting = false;
    bool requestRedock = false;
    bool followLatest = true;
    bool hasView = false;
    double xMin = 0.0;
    double xMax = 0.0;
    bool useSource1TimeAsX = true;
};

struct PlotGeneratorState {
    bool creating = false;
    bool createFF = false;
    int typeIndex = PlotType_Line;
    std::vector<PlotDataSourceRef> sources;
    char sourceQuery[128] = {0};
    int sourceSelected = -1;
    int activeSourceIndex = 0;
    bool useSource1TimeAsX = true;
    int nextId = 1;
    std::vector<GeneratedPlotWindow> windows;
};

PlotGeneratorState& generatorState() {
    static PlotGeneratorState state;
    return state;
}

constexpr const char* kPlotSettingsTypeName = "PhotonPlots";
constexpr const char* kPlotSettingsSectionName = "State";

int& persistedFontSize() {
    static int value = 24;
    return value;
}

void persistIniNowIfAvailable() {
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) { return; }
    ImGui::SaveIniSettingsToDisk("config.ini");
}

void clearPlotSettings() {
    PlotGeneratorState& state = generatorState();
    state.windows.clear();
    state.nextId = 1;
    persistedFontSize() = 24;
}

void* plotSettingsReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* name) {
    return (std::strcmp(name, kPlotSettingsSectionName) == 0) ? reinterpret_cast<void*>(1) : nullptr;
}

void plotSettingsReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line) {
    PlotGeneratorState& state = generatorState();
    if (std::strncmp(line, "FontSize=", 9) == 0) {
        persistedFontSize() = std::max(8, std::atoi(line + 9));
        return;
    }
    if (std::strncmp(line, "FontScale=", 10) == 0) {
        persistedFontSize() = std::max(8, static_cast<int>(std::lround(std::atof(line + 10) * 24.0)));
        return;
    }
    if (std::strncmp(line, "NextId=", 7) == 0) {
        state.nextId = std::max(1, std::atoi(line + 7));
        return;
    }
    if (std::strncmp(line, "Plot=", 5) != 0) {
        return;
    }

    std::stringstream ss(line + 5);
    std::string token;
    std::vector<std::string> tokens;
    while (std::getline(ss, token, ',')) {
        tokens.push_back(token);
    }
    if (tokens.size() < 4) {
        return;
    }

    GeneratedPlotWindow window;
    window.id = std::atoi(tokens[0].c_str());
    window.typeIndex = std::clamp(std::atoi(tokens[1].c_str()), 0, PlotType_Count - 1);
    window.open = (std::atoi(tokens[2].c_str()) != 0);
    const int sourceCount = std::max(0, std::atoi(tokens[3].c_str()));
    window.needsInitialDock = false;
    window.initialDockNode = 0;
    window.forceInitialDock = false;
    window.undockedInteracting = false;
    window.requestRedock = false;

    const size_t expected = 4 + static_cast<size_t>(sourceCount);
    const size_t available = std::min(tokens.size(), expected);
    for (size_t i = 4; i < available; ++i) {
        const std::string& src = tokens[i];
        const size_t colon = src.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        PlotDataSourceRef ref;
        ref.canId = std::atoi(src.substr(0, colon).c_str());
        ref.signalIndex = std::atoi(src.substr(colon + 1).c_str());
        window.sources.push_back(ref);
    }
    for (size_t i = available; i < tokens.size(); ++i) {
        const std::string& extra = tokens[i];
        if (extra.rfind("UseTimeX=", 0) == 0) {
            window.useSource1TimeAsX = (std::atoi(extra.c_str() + 9) != 0);
        }
    }

    state.nextId = std::max(state.nextId, window.id + 1);
    state.windows.push_back(std::move(window));
}

void plotSettingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out_buf) {
    const PlotGeneratorState& state = generatorState();
    out_buf->appendf("[%s][%s]\n", handler->TypeName, kPlotSettingsSectionName);
    out_buf->appendf("FontSize=%d\n", persistedFontSize());
    out_buf->appendf("NextId=%d\n", state.nextId);
    for (const GeneratedPlotWindow& plot : state.windows) {
        out_buf->appendf("Plot=%d,%d,%d,%d",
                         plot.id,
                         plot.typeIndex,
                         plot.open ? 1 : 0,
                         static_cast<int>(plot.sources.size()));
        for (const PlotDataSourceRef& source : plot.sources) {
            out_buf->appendf(",%d:%d", source.canId, source.signalIndex);
        }
        out_buf->appendf(",UseTimeX=%d", plot.useSource1TimeAsX ? 1 : 0);
        out_buf->append("\n");
    }
}

const PlotTypeSpec& specFor(int typeIndex) {
    return kPlotSpecs[static_cast<size_t>(typeIndex)];
}

int required3DSources(bool useSource1TimeAsX) {
    return useSource1TimeAsX ? 2 : 3;
}

const char* threeDMappingText(int typeIndex, bool useSource1TimeAsX) {
    const bool line = (typeIndex == PlotType_3DLine);
    if (useSource1TimeAsX) {
        return line
                   ? "3D line mapping: X = Source 1 time, Y = Source 1, Z = Source 2"
                   : "3D source mapping: X = Source 1 time, Y = Source 1, Z = Source 2";
    }
    return line
               ? "3D line mapping: Source 1 = X, Source 2 = Y, Source 3 = Z"
               : "3D source mapping: Source 1 = X, Source 2 = Y, Source 3 = Z";
}

const char* threeDRequiredSourcesText(int typeIndex, bool useSource1TimeAsX) {
    const bool line = (typeIndex == PlotType_3DLine);
    if (useSource1TimeAsX) {
        return line ? "3D line requires 2 sources (Y, Z)." : "3D plots require 2 valid sources (Y, Z).";
    }
    return line ? "3D line requires 3 sources (X, Y, Z)." : "3D plots require 3 valid sources (X, Y, Z).";
}

std::string threeDSourceLabel(size_t sourceIndex, bool useSource1TimeAsX) {
    if (useSource1TimeAsX) {
        return (sourceIndex == 0) ? "Y Source" : "Z Source";
    }
    if (sourceIndex == 0) { return "X Source"; }
    if (sourceIndex == 1) { return "Y Source"; }
    return "Z Source";
}

void apply3DSourceCount(bool useSource1TimeAsX,
                        std::vector<PlotDataSourceRef>& sources,
                        int& activeSourceIndex,
                        int& sourceSelected) {
    sources.resize(static_cast<size_t>(required3DSources(useSource1TimeAsX)));
    activeSourceIndex = std::clamp(activeSourceIndex, 0, std::max(0, static_cast<int>(sources.size()) - 1));
    sourceSelected = -1;
}

std::string generatedPlotWindowTitle(const GeneratedPlotWindow& plot) {
    return std::string(specFor(plot.typeIndex).label) + " " + std::to_string(plot.id);
}

ImGuiID largestLeafDockNode(ImGuiID nodeId) {
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(nodeId);
    if (!node) { return 0; }
    if (!node->IsSplitNode()) { return nodeId; }
    const ImGuiID leftId = largestLeafDockNode(node->ChildNodes[0] ? node->ChildNodes[0]->ID : 0);
    const ImGuiID rightId = largestLeafDockNode(node->ChildNodes[1] ? node->ChildNodes[1]->ID : 0);
    ImGuiDockNode* left = ImGui::DockBuilderGetNode(leftId);
    ImGuiDockNode* right = ImGui::DockBuilderGetNode(rightId);
    const float leftArea = left ? left->Size.x * left->Size.y : 0.0f;
    const float rightArea = right ? right->Size.x * right->Size.y : 0.0f;
    return (leftArea >= rightArea) ? leftId : rightId;
}

bool hasInputChar(ImWchar ch) {
    const ImGuiIO& io = ImGui::GetIO();
    for (ImWchar c : io.InputQueueCharacters) {
        if (c == ch) { return true; }
    }
    return false;
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

struct SourceMatch {
    int optionIndex = -1;
    int score = 0;
};

struct LinkedTimeAxisState {
    bool hasRange = false;
    bool followLatest = true;
    double xMin = 0.0;
    double xMax = 0.0;
};

struct ListWindowState {
    int selectedOption = -1;
};

LinkedTimeAxisState& linkedTimeAxisState() {
    static LinkedTimeAxisState state;
    return state;
}

std::unordered_map<int, ListWindowState>& listWindowStates() {
    static std::unordered_map<int, ListWindowState> states;
    return states;
}

std::vector<SourceMatch> buildSourceMatches(const std::vector<SignalOption>& options, const char* query) {
    std::vector<SourceMatch> matches;
    matches.reserve(options.size());

    const bool hasQuery = (query != nullptr && query[0] != '\0');
    for (size_t i = 0; i < options.size(); ++i) {
        SourceMatch m;
        m.optionIndex = static_cast<int>(i);
        m.score = hasQuery ? distance(query, options[i].label) : 0;
        matches.push_back(m);
    }

    std::sort(matches.begin(), matches.end(), [&](const SourceMatch& a, const SourceMatch& b) {
        if (a.score != b.score) { return a.score < b.score; }
        return options[static_cast<size_t>(a.optionIndex)].label <
               options[static_cast<size_t>(b.optionIndex)].label;
    });
    return matches;
}

void updateFollowState(GeneratedPlotWindow& plot, LinkedTimeAxisState* linkedState = nullptr) {
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
        if (linkedState != nullptr) {
            linkedState->followLatest = true;
        }
    }
    if (isNavigating) {
        plot.followLatest = false;
        if (linkedState != nullptr) {
            linkedState->followLatest = false;
        }
    }

    const ImPlotRect limits = ImPlot::GetPlotLimits(ImAxis_X1, ImAxis_Y1);
    plot.xMin = limits.X.Min;
    plot.xMax = limits.X.Max;
    plot.hasView = true;
    if (linkedState != nullptr) {
        linkedState->xMin = plot.xMin;
        linkedState->xMax = plot.xMax;
        linkedState->hasRange = true;
    }
}

void renderTimeSeriesPlot(Parse* parseINTF, GeneratedPlotWindow& plot) {
    if (plot.sources.empty()) {
        ImGui::TextUnformatted("Missing data sources.");
        return;
    }
    const CanMessage* primaryMsg = findMessage(parseINTF, plot.sources[0].canId);
    if (!primaryMsg) {
        ImGui::TextUnformatted("Data source is not available.");
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
    const bool useLinkedAxis = (plot.id > 0);
    LinkedTimeAxisState* linkedAxis = useLinkedAxis ? &linkedTimeAxisState() : nullptr;

    double rangeStart = liveWindowStart;
    double rangeEnd = latestTime;
    if (useLinkedAxis) {
        if (!linkedAxis->hasRange) {
            linkedAxis->xMin = rangeStart;
            linkedAxis->xMax = rangeEnd;
            linkedAxis->hasRange = true;
        }
        if (linkedAxis->followLatest) {
            linkedAxis->xMin = liveWindowStart;
            linkedAxis->xMax = latestTime;
        }
        rangeStart = std::max(dataStart, linkedAxis->xMin);
        rangeEnd = std::max(rangeStart, linkedAxis->xMax);
        if (rangeEnd > latestTime) {
            const double span = rangeEnd - rangeStart;
            rangeEnd = latestTime;
            rangeStart = std::max(dataStart, rangeEnd - span);
        }
    } else {
        if (!plot.followLatest && plot.hasView) {
            rangeStart = std::max(dataStart, plot.xMin);
            rangeEnd = std::max(rangeStart, plot.xMax);
            if (rangeEnd > latestTime) {
                const double span = rangeEnd - rangeStart;
                rangeEnd = latestTime;
                rangeStart = std::max(dataStart, rangeEnd - span);
            }
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

    if (useLinkedAxis) {
        linkedAxis->xMin = rangeStart;
        linkedAxis->xMax = rangeEnd;
        ImPlot::SetNextAxisLinks(ImAxis_X1, &linkedAxis->xMin, &linkedAxis->xMax);
    } else if (plot.followLatest) {
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
                const RenderSlice slice = makeRenderSlice(startIdx, usableEnd, kMaxRenderablePointsScatter2D);
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

    updateFollowState(plot, linkedAxis);
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
            const size_t start = (v.size() > kMaxRenderableHeatmapCells) ? (v.size() - kMaxRenderableHeatmapCells) : 0;
            const size_t usable = v.size() - start;
            const int cols = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(usable))));
            const int rows = std::max(1, static_cast<int>(usable / static_cast<size_t>(cols)));
            const int count = rows * cols;
            if (count > 0) {
                // Disable per-cell labels to avoid massive vertex counts in ImGui draw lists.
                ImPlot::PlotHeatmap(signals[0]->name.c_str(), v.data() + start, rows, cols, 0.0, 0.0, "");
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
        const size_t requiredSources = static_cast<size_t>(required3DSources(plot.useSource1TimeAsX));
        if (plot.sources.size() < requiredSources) {
            ImPlot3D::EndPlot();
            ImGui::TextUnformatted(threeDRequiredSourcesText(plot.typeIndex, plot.useSource1TimeAsX));
            return;
        }
        const CanMessage* timeMsg = findMessage(parseINTF, plot.sources[0].canId);
        const CanSignal* source1 = findSignal(parseINTF, plot.sources[0]);
        const CanSignal* source2 = findSignal(parseINTF, plot.sources[1]);
        const CanSignal* source3 = plot.useSource1TimeAsX ? nullptr : findSignal(parseINTF, plot.sources[2]);
        if (!timeMsg || !source1 || !source2 || (!plot.useSource1TimeAsX && !source3)) {
            ImPlot3D::EndPlot();
            ImGui::TextUnformatted("Missing source(s) for 3D line.");
            return;
        }

        const std::vector<double>& samplingTime = timeMsg->time;
        const std::vector<double>& xs = plot.useSource1TimeAsX ? samplingTime : source1->data;
        const std::vector<double>& ys = plot.useSource1TimeAsX ? source1->data : source2->data;
        const std::vector<double>& zs = plot.useSource1TimeAsX ? source2->data : source3->data;
        const size_t count = std::min({samplingTime.size(), xs.size(), ys.size(), zs.size()});
        if (count < 2) {
            ImPlot3D::EndPlot();
            ImGui::TextUnformatted("Need at least 2 points for 3D line.");
            return;
        }

        constexpr double maxTime = 5.0;
        const double latestTime = samplingTime[count - 1];
        const double windowStart = std::max(samplingTime[0], latestTime - maxTime);
        auto minIt = std::lower_bound(samplingTime.begin(), samplingTime.begin() + static_cast<std::ptrdiff_t>(count), windowStart);
        const size_t startIdx = static_cast<size_t>(std::distance(samplingTime.begin(), minIt));
        const RenderSlice slice = makeRenderSlice(startIdx, count, kMaxRenderablePoints3DLine);
        const int allowed = clampByBudget(slice.count * 2, slice.count, 2, 2);

        double xMin = std::numeric_limits<double>::max();
        double xMax = std::numeric_limits<double>::lowest();
        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();
        double zMin = std::numeric_limits<double>::max();
        double zMax = std::numeric_limits<double>::lowest();
        for (size_t i = slice.start; i < count; i += slice.step) {
            xMin = std::min(xMin, xs[i]);
            xMax = std::max(xMax, xs[i]);
            yMin = std::min(yMin, ys[i]);
            yMax = std::max(yMax, ys[i]);
            zMin = std::min(zMin, zs[i]);
            zMax = std::max(zMax, zs[i]);
        }
        if (std::abs(xMax - xMin) < 1e-6) { xMin -= 0.5; xMax += 0.5; }
        if (std::abs(yMax - yMin) < 1e-6) { yMin -= 0.5; yMax += 0.5; }
        if (std::abs(zMax - zMin) < 1e-6) { zMin -= 0.5; zMax += 0.5; }
        const double xPad = (xMax - xMin) * 0.1;
        const double yPad = (yMax - yMin) * 0.1;
        const double zPad = (zMax - zMin) * 0.1;
        ImPlot3D::SetupAxes(plot.useSource1TimeAsX ? "time" : source1->name.c_str(),
                            plot.useSource1TimeAsX ? source1->name.c_str() : source2->name.c_str(),
                            plot.useSource1TimeAsX ? source2->name.c_str() : source3->name.c_str());
        ImPlot3D::SetupAxesLimits(xMin - xPad, xMax + xPad, yMin - yPad, yMax + yPad, zMin - zPad, zMax + zPad, ImPlot3DCond_Always);

        if (allowed > 1) {
            ImPlot3D::PlotLine("3D Line", xs.data() + slice.start, ys.data() + slice.start, zs.data() + slice.start,
                               allowed, 0, 0, static_cast<int>(sizeof(double) * slice.step));
        }
    } else {
        const size_t requiredSources = static_cast<size_t>(required3DSources(plot.useSource1TimeAsX));
        std::vector<const CanSignal*> signals;
        signals.reserve(plot.sources.size());
        for (const PlotDataSourceRef& src : plot.sources) {
            const CanSignal* signal = findSignal(parseINTF, src);
            if (signal) { signals.push_back(signal); }
        }
        if (signals.size() < requiredSources) {
            ImPlot3D::EndPlot();
            ImGui::TextUnformatted(threeDRequiredSourcesText(plot.typeIndex, plot.useSource1TimeAsX));
            return;
        }
        const CanMessage* timeMsg = plot.useSource1TimeAsX ? findMessage(parseINTF, plot.sources[0].canId) : nullptr;
        if (plot.useSource1TimeAsX && !timeMsg) {
            ImPlot3D::EndPlot();
            ImGui::TextUnformatted("Missing source 1 message time for X axis.");
            return;
        }
        const std::vector<double>& xs = plot.useSource1TimeAsX ? timeMsg->time : signals[0]->data;
        const std::vector<double>& ys = plot.useSource1TimeAsX ? signals[0]->data : signals[1]->data;
        const std::vector<double>& zs = plot.useSource1TimeAsX ? signals[1]->data : signals[2]->data;
        const size_t count = std::min({xs.size(), ys.size(), zs.size()});
        if (count < 2) {
            ImPlot3D::EndPlot();
            ImGui::TextUnformatted("Need at least 2 points for 3D plotting.");
            return;
        }
        ImPlot3D::SetupAxes(plot.useSource1TimeAsX ? "time" : signals[0]->name.c_str(),
                            plot.useSource1TimeAsX ? signals[0]->name.c_str() : signals[1]->name.c_str(),
                            plot.useSource1TimeAsX ? signals[1]->name.c_str() : signals[2]->name.c_str());

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

void renderListWindow(Parse* parseINTF, GeneratedPlotWindow& plot, const std::vector<SignalOption>& options, bool allowAdd) {
    if (ImGui::BeginTable("##signalList", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableSetupColumn("Latest", ImGuiTableColumnFlags_WidthStretch, 0.3f);
        ImGui::TableHeadersRow();

        for (const PlotDataSourceRef& src : plot.sources) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(sourceName(parseINTF, src).c_str());

            ImGui::TableSetColumnIndex(1);
            const CanSignal* signal = findSignal(parseINTF, src);
            const char* valueText = "<n/a>";
            char valueBuf[64] = {0};
            if (signal && !signal->data.empty()) {
                std::snprintf(valueBuf, sizeof(valueBuf), "%.6g", signal->data.back());
                valueText = valueBuf;
            }
            const float colWidth = ImGui::GetColumnWidth();
            const float textWidth = ImGui::CalcTextSize(valueText).x;
            float x = ImGui::GetCursorPosX() + colWidth - textWidth - ImGui::GetStyle().CellPadding.x * 2.0f;
            if (x < ImGui::GetCursorPosX()) { x = ImGui::GetCursorPosX(); }
            ImGui::SetCursorPosX(x);
            ImGui::TextUnformatted(valueText);
        }
        ImGui::EndTable();
    }

    if (!allowAdd) { return; }
    ImGui::Separator();
    ImGui::TextUnformatted("Add Signal");

    if (options.empty()) {
        ImGui::TextUnformatted("No CAN signals available yet.");
        return;
    }

    ListWindowState& state = listWindowStates()[plot.id];
    if (state.selectedOption < 0 || state.selectedOption >= static_cast<int>(options.size())) {
        state.selectedOption = 0;
    }

    ImGui::SetNextItemWidth(-110.0f);
    const char* preview = options[static_cast<size_t>(state.selectedOption)].label.c_str();
    if (ImGui::BeginCombo("##listAddSignal", preview)) {
        for (int i = 0; i < static_cast<int>(options.size()); ++i) {
            const bool selected = (state.selectedOption == i);
            if (ImGui::Selectable(options[static_cast<size_t>(i)].label.c_str(), selected)) {
                state.selectedOption = i;
            }
            if (selected) { ImGui::SetItemDefaultFocus(); }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add") && state.selectedOption >= 0 && state.selectedOption < static_cast<int>(options.size())) {
        plot.sources.push_back(options[static_cast<size_t>(state.selectedOption)].ref);
        ImGui::MarkIniSettingsDirty();
        persistIniNowIfAvailable();
    }
}

void drawGeneratedPlots(Parse* parseINTF, ImGuiID customDockspaceId, bool customVisible) {
    PlotGeneratorState& state = generatorState();
    const std::vector<SignalOption> options = collectSignalOptions(parseINTF);
    const size_t countBefore = state.windows.size();
    auto pruneListWindowStates = [&]() {
        auto& listStates = listWindowStates();
        for (auto it = listStates.begin(); it != listStates.end();) {
            const bool exists = std::any_of(state.windows.begin(), state.windows.end(),
                                            [&](const GeneratedPlotWindow& w) { return w.id == it->first; });
            if (!exists) {
                it = listStates.erase(it);
            } else {
                ++it;
            }
        }
    };

    if (customDockspaceId != 0 && ImGui::DockBuilderGetNode(customDockspaceId) != nullptr) {
        bool hasEstablishedLayout = false;
        for (const GeneratedPlotWindow& plot : state.windows) {
            if (plot.open && !plot.needsInitialDock) {
                hasEstablishedLayout = true;
                break;
            }
        }

        bool dockChanged = false;
        for (GeneratedPlotWindow& plot : state.windows) {
            if (!plot.open || !plot.needsInitialDock) { continue; }
            const std::string windowTitle = generatedPlotWindowTitle(plot);
            ImGuiID targetNode = customDockspaceId;

            if (hasEstablishedLayout) {
                ImGuiID leaf = largestLeafDockNode(customDockspaceId);
                ImGuiDockNode* leafNode = ImGui::DockBuilderGetNode(leaf);
                if (leaf != 0 && leafNode != nullptr) {
                    const ImGuiDir splitDir = (leafNode->Size.x >= leafNode->Size.y) ? ImGuiDir_Right : ImGuiDir_Down;
                    constexpr float kGoldenNewPaneRatio = 0.381966f;
                    ImGuiID newNode = 0;
                    ImGuiID remainingNode = 0;
                    ImGui::DockBuilderSplitNode(leaf, splitDir, kGoldenNewPaneRatio, &newNode, &remainingNode);
                    if (newNode != 0) {
                        targetNode = newNode;
                    }
                }
            }

            ImGui::DockBuilderDockWindow(windowTitle.c_str(), targetNode);
            plot.initialDockNode = targetNode;
            plot.forceInitialDock = true;
            plot.needsInitialDock = false;
            hasEstablishedLayout = true;
            dockChanged = true;
        }

        if (dockChanged) {
            ImGui::DockBuilderFinish(customDockspaceId);
            ImGui::MarkIniSettingsDirty();
            persistIniNowIfAvailable();
        }
    }

    // Do not submit plot windows while Custom is inactive. Submitting docked windows can
    // force-tab back to Custom. Track undocked ones and recover when Custom is active again.
    if (!customVisible) {
        for (GeneratedPlotWindow& plot : state.windows) {
            if (!plot.open) { continue; }
            const std::string windowTitle = generatedPlotWindowTitle(plot);
            ImGuiWindow* existing = ImGui::FindWindowByName(windowTitle.c_str());
            const bool isUndocked = (existing != nullptr) && (existing->DockNode == nullptr);
            if (isUndocked) {
                plot.requestRedock = true;
            }
        }
        state.windows.erase(std::remove_if(state.windows.begin(), state.windows.end(),
            [](const GeneratedPlotWindow& window) { return !window.open; }),
            state.windows.end());
        if (state.windows.size() != countBefore) {
            ImGui::MarkIniSettingsDirty();
            persistIniNowIfAvailable();
        }
        pruneListWindowStates();
        return;
    }

    for (GeneratedPlotWindow& plot : state.windows) {
        std::string windowTitle = generatedPlotWindowTitle(plot);
        ImGuiWindow* existing = ImGui::FindWindowByName(windowTitle.c_str());
        const bool wasFloatingDockNode =
            (existing != nullptr) &&
            (existing->DockNode != nullptr) &&
            existing->DockNode->IsFloatingNode();
        const bool wasDocked = (existing != nullptr) && (existing->DockNode != nullptr) && !wasFloatingDockNode;

        if (customDockspaceId != 0) {
            if (plot.forceInitialDock && plot.initialDockNode != 0) {
                ImGui::SetNextWindowDockID(plot.initialDockNode, ImGuiCond_Always);
            } else if (plot.requestRedock) {
                ImGui::SetNextWindowDockID(customDockspaceId, ImGuiCond_Always);
            }
        }
        ImGuiWindowFlags plotWindowFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse;
        if (wasDocked) {
            plotWindowFlags |= ImGuiWindowFlags_NoBackground;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, wasDocked ? ImVec2(1.0f, 1.0f) : ImVec2(6.0f, 6.0f));
        if (ImGui::Begin(windowTitle.c_str(), &plot.open, plotWindowFlags)) {
            plot.forceInitialDock = false;
            ImGuiWindow* current = ImGui::GetCurrentWindow();
            const bool nowFloatingDockNode =
                (current != nullptr) &&
                (current->DockNode != nullptr) &&
                current->DockNode->IsFloatingNode();
            const bool nowDocked = (current != nullptr) && (current->DockNode != nullptr) && !nowFloatingDockNode;
            if (nowDocked) {
                // Clear recovery latch once docked in any node of the dock tree.
                plot.requestRedock = false;
            } else if (current != nullptr) {
                // Keep floating generated plots visible above the full-size host tabs.
                ImGui::BringWindowToDisplayFront(current);
            }
            if (specFor(plot.typeIndex).is3D) {
                render3DPlot(parseINTF, plot);
            } else if (plot.typeIndex == PlotType_List) {
                renderListWindow(parseINTF, plot, options, true);
            } else if (specFor(plot.typeIndex).usesTimeAxis) {
                renderTimeSeriesPlot(parseINTF, plot);
            } else {
                renderNonTimePlot(parseINTF, plot);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleVar();
    }
    state.windows.erase(std::remove_if(state.windows.begin(), state.windows.end(),
        [](const GeneratedPlotWindow& window) { return !window.open; }),
        state.windows.end());
    if (state.windows.size() != countBefore) {
        ImGui::MarkIniSettingsDirty();
        persistIniNowIfAvailable();
    }
    pruneListWindowStates();
}

void drawGeneratorUI(Parse* parseINTF) {
    PlotGeneratorState& state = generatorState();
    const std::vector<SignalOption> options = collectSignalOptions(parseINTF);

    const ImGuiIO& io = ImGui::GetIO();
    const bool generatorHotkeyPressed =
        ImGui::IsKeyPressed(ImGuiKey_Backslash) ||
        hasInputChar('\\') ||
        hasInputChar('|');

    if (!state.creating && generatorHotkeyPressed && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper) {
        state.creating = true;
        state.createFF = true;
        state.typeIndex = PlotType_Line;
        state.sources.assign(static_cast<size_t>(specFor(state.typeIndex).minSources), PlotDataSourceRef{});
        state.useSource1TimeAsX = true;
        state.sourceQuery[0] = '\0';
        state.sourceSelected = -1;
        state.activeSourceIndex = 0;
    }

    if (!state.creating) { return; }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 center = vp ? vp->GetCenter() : ImVec2(0, 0);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoNav;

    bool windowFocused = false;
    bool inputActive = false;
    const bool justOpened = state.createFF;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    if (!ImGui::Begin("Configure Plot", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        state.creating = false;
    }
    windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGui::SeparatorText("Configure");

    const PlotTypeSpec& spec = specFor(state.typeIndex);
    int minSourcesForCurrentConfig = spec.minSources;
    int maxSourcesForCurrentConfig = spec.maxSources;
    if (spec.is3D) {
        minSourcesForCurrentConfig = required3DSources(state.useSource1TimeAsX);
        maxSourcesForCurrentConfig = minSourcesForCurrentConfig;
    }
    if (ImGui::BeginTable("##configureLayout", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 0.34f);
        ImGui::TableSetupColumn("Assigned", ImGuiTableColumnFlags_WidthStretch, 0.34f);
        ImGui::TableSetupColumn("Search", ImGuiTableColumnFlags_WidthStretch, 0.32f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Plot Type");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::BeginCombo("##plotType", specFor(state.typeIndex).label)) {
            for (int i = 0; i < PlotType_Count; ++i) {
                const bool selected = (state.typeIndex == i);
                if (ImGui::Selectable(specFor(i).label, selected)) {
                    state.typeIndex = i;
                    resetGeneratorSourcesForType(state);
                    if (specFor(state.typeIndex).is3D) {
                        apply3DSourceCount(state.useSource1TimeAsX, state.sources, state.activeSourceIndex, state.sourceSelected);
                    }
                    state.activeSourceIndex = std::clamp(state.activeSourceIndex, 0, std::max(0, static_cast<int>(state.sources.size()) - 1));
                }
                if (selected) { ImGui::SetItemDefaultFocus(); }
            }
            ImGui::EndCombo();
        }

        const float previewHeight = 220.0f;
        if (ImGui::BeginChild("##plotPreview", ImVec2(0.0f, previewHeight), true)) {
            GeneratedPlotWindow previewPlot;
            previewPlot.id = -1000 - state.typeIndex;
            previewPlot.typeIndex = state.typeIndex;
            previewPlot.sources = state.sources;
            previewPlot.useSource1TimeAsX = state.useSource1TimeAsX;
            if (specFor(previewPlot.typeIndex).is3D) {
                render3DPlot(parseINTF, previewPlot);
            } else if (previewPlot.typeIndex == PlotType_List) {
                renderListWindow(parseINTF, previewPlot, options, false);
            } else if (specFor(previewPlot.typeIndex).usesTimeAxis) {
                renderTimeSeriesPlot(parseINTF, previewPlot);
            } else {
                renderNonTimePlot(parseINTF, previewPlot);
            }
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("Minimum Signals: %d", minSourcesForCurrentConfig);
        ImGui::Text("Maximum Signals: %d", maxSourcesForCurrentConfig);
        if (spec.is3D) {
            if (ImGui::Checkbox("Use Source 1 Time as X", &state.useSource1TimeAsX)) {
                apply3DSourceCount(state.useSource1TimeAsX, state.sources, state.activeSourceIndex, state.sourceSelected);
            }
            ImGui::TextUnformatted(threeDMappingText(state.typeIndex, state.useSource1TimeAsX));
        } else {
            if (state.typeIndex == PlotType_List) {
                ImGui::TextUnformatted("List shows latest value for each assigned signal.");
            } else {
                ImGui::Text("Time Inherited From: Signal 1");
            }
        }

        if (maxSourcesForCurrentConfig > minSourcesForCurrentConfig) {
            if (ImGui::Button("Add Signal") && state.sources.size() < static_cast<size_t>(maxSourcesForCurrentConfig)) {
                state.sources.push_back({});
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove Signal") && state.sources.size() > static_cast<size_t>(minSourcesForCurrentConfig)) {
                state.sources.pop_back();
                state.activeSourceIndex = std::clamp(state.activeSourceIndex, 0, std::max(0, static_cast<int>(state.sources.size()) - 1));
                state.sourceSelected = -1;
            }
        }

        ImGui::SeparatorText("Assigned Signals");
        for (size_t i = 0; i < state.sources.size(); ++i) {
            const bool selectedRow = (static_cast<int>(i) == state.activeSourceIndex);
            int selectedIndex = findOptionIndex(options, state.sources[i]);
            std::string sourceLabel = (selectedIndex >= 0) ? options[static_cast<size_t>(selectedIndex)].label : "<unassigned>";
            std::string lineLabel;
            if (spec.is3D) {
                lineLabel = threeDSourceLabel(i, state.useSource1TimeAsX);
            } else {
                lineLabel = "Source " + std::to_string(i + 1);
            }
            std::string rowText = lineLabel + ": " + sourceLabel;
            if (ImGui::Selectable(rowText.c_str(), selectedRow, ImGuiSelectableFlags_AllowDoubleClick)) {
                state.activeSourceIndex = static_cast<int>(i);
                state.createFF = true;
            }
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("Search: ");
        if (options.empty()) {
            ImGui::TextUnformatted("No CAN signals available yet.");
        } else if (!state.sources.empty()) {
            state.activeSourceIndex = std::clamp(state.activeSourceIndex, 0, std::max(0, static_cast<int>(state.sources.size()) - 1));
            if (state.createFF) {
                ImGui::SetKeyboardFocusHere();
            }
            bool submitted = ImGui::InputText("##plotSourceSearch", state.sourceQuery, IM_ARRAYSIZE(state.sourceQuery),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
            if (state.createFF) { state.createFF = false; }
            inputActive = ImGui::IsItemActive();

            std::vector<SourceMatch> matches = buildSourceMatches(options, state.sourceQuery);
            if (!matches.empty()) {
                if (state.sourceSelected < 0 || state.sourceSelected >= static_cast<int>(matches.size())) {
                    state.sourceSelected = 0;
                }

                const int resultCount = static_cast<int>(matches.size());
                const int previousSelected = state.sourceSelected;
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) state.sourceSelected = (state.sourceSelected + 1) % resultCount;
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) state.sourceSelected = (state.sourceSelected - 1 + resultCount) % resultCount;
                if (ImGui::IsKeyPressed(ImGuiKey_Tab)) state.sourceSelected = (state.sourceSelected + 1) % resultCount;
                const bool selectionMovedByKeyboard = (state.sourceSelected != previousSelected);

                float rowHeight = ImGui::GetTextLineHeightWithSpacing();
                const int rowsToShow = std::min(resultCount, 6);
                ImVec2 listSize(ImGui::GetContentRegionAvail().x, rowHeight * rowsToShow + ImGui::GetStyle().FramePadding.y);
                if (ImGui::BeginListBox("##plotSourceResults", listSize)) {
                    for (int i = 0; i < resultCount; ++i) {
                        const int optionIndex = matches[static_cast<size_t>(i)].optionIndex;
                        const bool isSelected = (i == state.sourceSelected);
                        if (ImGui::Selectable(options[static_cast<size_t>(optionIndex)].label.c_str(), isSelected)) {
                            state.sourceSelected = i;
                            state.sources[static_cast<size_t>(state.activeSourceIndex)] = options[static_cast<size_t>(optionIndex)].ref;
                        }
                        if (isSelected && selectionMovedByKeyboard) {
                            ImGui::SetScrollHereY();
                        }
                    }
                    ImGui::EndListBox();
                }

                const bool activateSelection = (submitted || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
                if (activateSelection && state.sourceSelected >= 0 && state.sourceSelected < resultCount) {
                    const int optionIndex = matches[static_cast<size_t>(state.sourceSelected)].optionIndex;
                    state.sources[static_cast<size_t>(state.activeSourceIndex)] = options[static_cast<size_t>(optionIndex)].ref;
                }
            } else {
                state.sourceSelected = -1;
            }
        }

        ImGui::EndTable();
    }

    bool allSelected = !state.sources.empty();
    for (const PlotDataSourceRef& src : state.sources) {
        if (findOptionIndex(options, src) < 0) {
            allSelected = false;
            break;
        }
    }
    minSourcesForCurrentConfig = spec.minSources;
    maxSourcesForCurrentConfig = spec.maxSources;
    if (spec.is3D) {
        minSourcesForCurrentConfig = required3DSources(state.useSource1TimeAsX);
        maxSourcesForCurrentConfig = minSourcesForCurrentConfig;
    }
    const bool validCount = state.sources.size() >= static_cast<size_t>(minSourcesForCurrentConfig) &&
                            state.sources.size() <= static_cast<size_t>(maxSourcesForCurrentConfig);
    const bool canCreate = allSelected && validCount;

    if (!canCreate) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Create")) {
        GeneratedPlotWindow window;
        window.id = state.nextId++;
        window.typeIndex = state.typeIndex;
        window.sources = state.sources;
        window.useSource1TimeAsX = state.useSource1TimeAsX;
        state.windows.push_back(std::move(window));
        ImGui::MarkIniSettingsDirty();
        persistIniNowIfAvailable();
        state.creating = false;
    }
    if (!canCreate) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        state.creating = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    const bool anyPopupOpen = ImGui::IsPopupOpen((const char*)nullptr, ImGuiPopupFlags_AnyPopupId);
    if (!windowFocused && !inputActive && !anyPopupOpen && !justOpened) {
        state.creating = false;
    }
}
} // namespace

void UI::installPersistentSettings() {
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) { return; }
    const ImGuiID typeHash = ImHashStr(kPlotSettingsTypeName);
    for (ImGuiSettingsHandler& existing : ctx->SettingsHandlers) {
        if (existing.TypeHash == typeHash) {
            return;
        }
    }

    ImGuiSettingsHandler handler;
    handler.TypeName = kPlotSettingsTypeName;
    handler.TypeHash = typeHash;
    handler.ClearAllFn = [](ImGuiContext*, ImGuiSettingsHandler*) { clearPlotSettings(); };
    handler.ReadOpenFn = plotSettingsReadOpen;
    handler.ReadLineFn = plotSettingsReadLine;
    handler.WriteAllFn = plotSettingsWriteAll;
    ImGui::AddSettingsHandler(&handler);
}

void UI::setScale() {
    const ImGuiIO& io = ImGui::GetIO();
    if (!fontSizeSynced) {
        const int persisted = std::clamp(persistedFontSize(), fontSizeMin, fontSizeMax);
        if (fontSize != persisted) {
            fontSize = persisted;
            fontSizeDirty = true;
        }
        fontSizeSynced = true;
    }

    const bool zoomIn = io.KeyCtrl &&
        (ImGui::IsKeyReleased(ImGuiKey_Equal) || ImGui::IsKeyReleased(ImGuiKey_KeypadAdd));
    const bool zoomOut = io.KeyCtrl &&
        (ImGui::IsKeyReleased(ImGuiKey_Minus) || ImGui::IsKeyReleased(ImGuiKey_KeypadSubtract));

    int nextSize = fontSize;
    if (zoomIn) {
        nextSize = std::clamp(fontSize + 1, fontSizeMin, fontSizeMax);
    } else if (zoomOut) {
        nextSize = std::clamp(fontSize - 1, fontSizeMin, fontSizeMax);
    }

    if (nextSize != fontSize) {
        fontSize = nextSize;
        fontSizeDirty = true;
    }

    if (persistedFontSize() != fontSize) {
        persistedFontSize() = fontSize;
        ImGui::MarkIniSettingsDirty();
        persistIniNowIfAvailable();
    }
}

void UI::build(){
    static bool showFps = false;
    ImGui::NewFrame();
    setScale();

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

    ImGuiWindowFlags main_dockspace_flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings;

    ImGuiID mainDockspaceId = 0;
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);

    if (ImGui::Begin("Main Host##MainDockHost", nullptr, main_dockspace_flags)) {
        mainDockspaceId = ImGui::GetID("MainDockspace");
        const ImGuiDockNodeFlags lockedTabsOnlyFlags =
            ImGuiDockNodeFlags_NoUndocking |
            ImGuiDockNodeFlags_NoDockingSplit |
            ImGuiDockNodeFlags_NoDockingOverMe |
            ImGuiDockNodeFlags_NoDockingOverOther;
        ImGui::DockSpace(mainDockspaceId, ImVec2(0.0f, 0.0f), lockedTabsOnlyFlags);

        if (ImGui::DockBuilderGetNode(mainDockspaceId) == nullptr) {
            ImGui::DockBuilderRemoveNode(mainDockspaceId);
            ImGui::DockBuilderAddNode(mainDockspaceId, ImGuiDockNodeFlags_DockSpace | lockedTabsOnlyFlags);
            ImGui::DockBuilderSetNodeSize(mainDockspaceId, ImGui::GetContentRegionAvail());
            ImGui::DockBuilderDockWindow("Home##MainDockedTab", mainDockspaceId);
            ImGui::DockBuilderDockWindow("Custom##CustomDockedTab", mainDockspaceId);
            ImGui::DockBuilderDockWindow("Debug##DebugDockedTab", mainDockspaceId);
            ImGui::DockBuilderFinish(mainDockspaceId);
        }
    }
    ImGui::End();

    if (mainDockspaceId != 0) ImGui::SetNextWindowDockID(mainDockspaceId, ImGuiCond_FirstUseEver);
    ImGuiWindowFlags fixedTabFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("Home##MainDockedTab", nullptr, fixedTabFlags)) home();
    ImGui::End();
    if (mainDockspaceId != 0) ImGui::SetNextWindowDockID(mainDockspaceId, ImGuiCond_FirstUseEver);

    ImGuiID customDockspaceId = 0;
    bool customVisible = ImGui::Begin("Custom##CustomDockedTab", nullptr, fixedTabFlags);
    customDockspaceId = ImGui::GetID("CustomDockspace");
    const ImGuiDockNodeFlags customTabsOnlyFlags = ImGuiDockNodeFlags_AutoHideTabBar;
    const ImGuiDockNodeFlags customDockFlags = customVisible
        ? customTabsOnlyFlags
        : (customTabsOnlyFlags | ImGuiDockNodeFlags_KeepAliveOnly);
    ImGui::DockSpace(customDockspaceId, ImVec2(0.0f, 0.0f), customDockFlags);
    if (customVisible) emptyCustom();
    ImGui::End();

    if (mainDockspaceId != 0) ImGui::SetNextWindowDockID(mainDockspaceId, ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Debug##DebugDockedTab", nullptr, fixedTabFlags)){
        ImGui::Text("Hi :0");
    } ImGui::End();

    drawGeneratedPlots(parseINTF, customDockspaceId, customVisible);
    signalSearch();
    drawGeneratorUI(parseINTF);
    terminal();

    if(ImGui::IsKeyReleased(ImGuiKey_F3)) showFps = !showFps;
    if(showFps) fpsWindow();
    ImGui::Render();
}

void UI::emptyCustom(){
    const PlotGeneratorState& generatedPlots = generatorState();
    bool hasOpenGeneratedPlot = false;
    for (const GeneratedPlotWindow& plot : generatedPlots.windows) {
        if (plot.open) {
            hasOpenGeneratedPlot = true;
            break;
        }
    }
    if (!hasOpenGeneratedPlot) {
        const char* emptyText = "Press '\\' to create plots";
        ImVec2 textSize = ImGui::CalcTextSize(emptyText);
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();
        ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        const float targetY = (mainViewport != nullptr)
            ? (mainViewport->Pos.y + (mainViewport->Size.y * 0.33f))
            : (windowPos.y + (windowSize.y * 0.33f));
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(windowPos.x + (windowSize.x * 0.5f) - (textSize.x * 0.5f), targetY - (textSize.y * 0.5f)),
            IM_COL32(255, 255, 255, 255),
            emptyText);
    }
}

void UI::networkUI(){
        static int dbcIdx = 0;
        static int networkIdx = 2;
        ImGui::Combo("DBC", &dbcIdx, availableDBC.data(), availableDBC.size());
        if(dbcIdx == 0){ currentDBC = "assettoCorsa"; }
        if(dbcIdx == 1){ currentDBC = "daybreak"; }

        ImGui::Combo("Network", &networkIdx, availableNetwork.data(), availableNetwork.size());
        if(networkIdx == 0){ currentNetwork = "Server";}
        if(networkIdx == 1){ currentNetwork = "Serial";}
        if(networkIdx == 2){ currentNetwork = "Assetto Corsa";}
        if(currentNetwork == "Serial"){
            static int baudIdx = 5;
            static int portIdx = 0;
            ImGui::Combo("Baud Rate", &baudIdx, baudRates.data(), baudRates.size());
            if(baudRate != baudRates[baudIdx]){
                rebuildSerial = true;
                baudRate = baudRates[baudIdx];
            }
            refreshSerialPorts();
            if(ports.empty()){
                ImGui::Text("No serial ports detected");
            } else {
                if(portIdx < 0 || static_cast<size_t>(portIdx) >= ports.size()){ portIdx = 0; }
                ImGui::Combo("Serial Port", &portIdx, ports.data(), ports.size());
                if(portIdx >= 0 && static_cast<size_t>(portIdx) < discoveredSerialPorts.size()){
                    if(serialPort != discoveredSerialPorts[portIdx]){
                        rebuildSerial = true;
                        serialPort = discoveredSerialPorts[portIdx];
                    }
                }
            }
        }
}

void UI::home(){
    const ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
    const ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
    const float contentWidth = contentMax.x - contentMin.x;
    const float targetWidth = std::max(240.0f, contentWidth * 0.15f);

    static float networkPanelHeight = 0.0f;
    if(networkPanelHeight <= 0.0f){
        networkPanelHeight = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    }

    const float x = contentMin.x + ((contentWidth - targetWidth) * 0.5f);
    const float y = std::max(contentMin.y, contentMax.y - networkPanelHeight);
    ImGui::SetCursorPos(ImVec2(x, y));
    ImGui::PushItemWidth(targetWidth);
    ImGui::BeginGroup();
    networkUI();
    ImGui::EndGroup();
    networkPanelHeight = ImGui::GetItemRectSize().y;
    ImGui::PopItemWidth();
}

void UI::refreshSerialPorts(){
    static bool windowsPortsInitialized = false;
#ifndef _WIN32
    static auto lastRefresh = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if(lastRefresh != std::chrono::steady_clock::time_point{} &&
       (now - lastRefresh) < std::chrono::milliseconds(1000) &&
       !ports.empty()){
        return;
    }
    lastRefresh = now;
#else
    if(windowsPortsInitialized && !ports.empty()){
        return;
    }
#endif

    std::vector<std::string> nextPorts;

#ifdef _WIN32
    auto parseComIndex = [](const std::string& name)->int{
        if(name.size() <= 3){ return 0; }
        return std::atoi(name.c_str() + 3);
    };

    // QueryDosDevice lets us enumerate all COM symbolic links without touching each port.
    constexpr DWORD kDeviceBufferSize = 32768;
    std::array<char, kDeviceBufferSize> deviceNames{};
    const DWORD charsWritten = QueryDosDeviceA(nullptr, deviceNames.data(), kDeviceBufferSize);
    if(charsWritten != 0){
        const char* current = deviceNames.data();
        while(*current != '\0'){
            std::string deviceName(current);
            if(deviceName.rfind("COM", 0) == 0){
                nextPorts.push_back(deviceName);
            }
            current += deviceName.size() + 1;
        }
        std::sort(nextPorts.begin(), nextPorts.end(), [&](const std::string& lhs, const std::string& rhs){
            const int leftIdx = parseComIndex(lhs);
            const int rightIdx = parseComIndex(rhs);
            if(leftIdx == rightIdx){ return lhs < rhs; }
            return leftIdx < rightIdx;
        });
        nextPorts.erase(std::unique(nextPorts.begin(), nextPorts.end()), nextPorts.end());
    }

    if(nextPorts.empty()){
        for(int i = 1; i <= 64; ++i){
            std::string name = "COM" + std::to_string(i);
            COMMCONFIG commConfig = {};
            DWORD commConfigSize = sizeof(commConfig);
            if(GetDefaultCommConfigA(name.c_str(), &commConfig, &commConfigSize) != 0){
                nextPorts.push_back(name);
            }
        }
    }
    windowsPortsInitialized = true;
#else
    namespace fs = std::filesystem;
    std::error_code ec;
    for(const auto& entry : fs::directory_iterator("/dev", ec)){
        if(ec){ break; }
        const std::string name = entry.path().filename().string();
        if(name.rfind("ttyUSB", 0) == 0 || name.rfind("ttyACM", 0) == 0 || name.rfind("ttyS", 0) == 0){
            nextPorts.push_back(entry.path().string());
        }
    }
    std::sort(nextPorts.begin(), nextPorts.end());
#endif

    if(nextPorts.empty()){
#ifdef _WIN32
        nextPorts.push_back(serialPort.empty() ? "COM1" : serialPort);
#else
        nextPorts.push_back(serialPort.empty() ? "/dev/ttyUSB0" : serialPort);
#endif
    }

    if(nextPorts == discoveredSerialPorts && !ports.empty()){ return; }

    discoveredSerialPorts = std::move(nextPorts);
    ports.clear();
    ports.reserve(discoveredSerialPorts.size());
    for(const std::string& port : discoveredSerialPorts){
        ports.push_back(port.c_str());
    }

    auto current = std::find(discoveredSerialPorts.begin(), discoveredSerialPorts.end(), serialPort);
    if(current == discoveredSerialPorts.end()){
        serialPort = discoveredSerialPorts.front();
    }
}

void UI::bottomBar(){
    const ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
    const ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
    const float rowHeight = ImGui::GetFrameHeight() + 6.0f;
    const float rowY = contentMax.y - rowHeight;

    ImGui::SetCursorPos(ImVec2(contentMin.x, rowY));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));

    const ImGuiWindowFlags rowFlags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::BeginChild("##bottom_bar_row", ImVec2(0.0f, rowHeight), false, rowFlags)) {
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
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

void UI::terminal() {
    const ImGuiIO& io = ImGui::GetIO();
    bool terminalHotkeyPressed =
        ImGui::IsKeyPressed(ImGuiKey_GraveAccent) ||
        hasInputChar('~');

    if (terminalHotkeyPressed && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper) {
        showImGuiTerminalDemo = !showImGuiTerminalDemo;
    }

    if (showImGuiTerminalDemo) {
        console.Draw("console", &terminalHotkeyPressed);
    }
}

void UI::nodeEditorDemo() {
    if (nodeEditorContext == nullptr) {
        return;
    }

    namespace ed = ax::NodeEditor;
    struct DemoLink {
        ed::LinkId id;
        ed::PinId inputId;
        ed::PinId outputId;
    };

    static std::vector<DemoLink> links;
    static int nextLinkId = 100;

    ed::SetCurrentEditor(nodeEditorContext);
    ed::Begin("Node Editor Demo");

    const ed::NodeId nodeAId = 1;
    const ed::PinId nodeAInputId = 2;
    const ed::PinId nodeAOutputId = 3;

    const ed::NodeId nodeBId = 4;
    const ed::PinId nodeBInput1Id = 5;
    const ed::PinId nodeBInput2Id = 6;
    const ed::PinId nodeBOutputId = 7;

    if (nodeEditorFirstFrame) {
        ed::SetNodePosition(nodeAId, ImVec2(20.0f, 20.0f));
        ed::SetNodePosition(nodeBId, ImVec2(260.0f, 80.0f));
    }

    ed::BeginNode(nodeAId);
    ImGui::Text("Node A");
    ed::BeginPin(nodeAInputId, ed::PinKind::Input);
    ImGui::Text("-> In");
    ed::EndPin();
    ImGui::SameLine();
    ed::BeginPin(nodeAOutputId, ed::PinKind::Output);
    ImGui::Text("Out ->");
    ed::EndPin();
    ed::EndNode();

    ed::BeginNode(nodeBId);
    ImGui::Text("Node B");
    ed::BeginPin(nodeBInput1Id, ed::PinKind::Input);
    ImGui::Text("-> In1");
    ed::EndPin();
    ed::BeginPin(nodeBInput2Id, ed::PinKind::Input);
    ImGui::Text("-> In2");
    ed::EndPin();
    ed::BeginPin(nodeBOutputId, ed::PinKind::Output);
    ImGui::Text("Out ->");
    ed::EndPin();
    ed::EndNode();

    for (const DemoLink& link : links) {
        ed::Link(link.id, link.inputId, link.outputId);
    }

    if (ed::BeginCreate()) {
        ed::PinId inputPinId;
        ed::PinId outputPinId;
        if (ed::QueryNewLink(&inputPinId, &outputPinId) && inputPinId && outputPinId) {
            if (ed::AcceptNewItem()) {
                links.push_back({ed::LinkId(nextLinkId++), inputPinId, outputPinId});
            }
        }
    }
    ed::EndCreate();

    if (ed::BeginDelete()) {
        ed::LinkId deletedLinkId;
        while (ed::QueryDeletedLink(&deletedLinkId)) {
            if (ed::AcceptDeletedItem()) {
                links.erase(std::remove_if(links.begin(), links.end(),
                           [&](const DemoLink& link) { return link.id == deletedLinkId; }),
                           links.end());
            }
        }
    }
    ed::EndDelete();

    ed::End();
    if (nodeEditorFirstFrame) {
        ed::NavigateToContent(0.0f);
        nodeEditorFirstFrame = false;
    }
    ed::SetCurrentEditor(nullptr);
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
                             ImGuiWindowFlags_NoNav;

    bool windowFocused = false;
    bool inputActive = false;
    bool hidePrompt = false;
    const bool justOpened = cmdFF;

    if(cmdOpen){
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
        ImGui::SetNextWindowBgAlpha(0.90f);
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
    if(cmdShowPopup){ popupFocused = signalSearchPopup(); } // this is what you are looking for
    if(hidePrompt){ cmdOpen = false; }
    if(!windowFocused && !inputActive && !popupFocused && !justOpened){
        cmdOpen = false;
        cmdShowPopup = false;
    }
}

bool UI::signalSearchPopup(){
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | 
                             ImGuiWindowFlags_NoSavedSettings  | 
                             ImGuiWindowFlags_NoTitleBar | 
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoMove;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
    ImGui::SetNextWindowBgAlpha(0.90f);

    bool focused = false;
    bool childFocused = false;
    static int selected = 0;
    static ImVec2 popupWindowSize(360.0f, 420.0f);
    static ImVec2 signalSearchPlotSize(900.0f, 330.0f);
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
    const float totalWidth = popupWindowSize.x + gap + signalSearchPlotSize.x;
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
                        const float groupWidth = popupWindowSize.x + gap + signalSearchPlotSize.x;
                        ImVec2 groupOrigin(center.x - groupWidth * 0.5f, center.y);
                        ImVec2 signalSearchPlotPos(groupOrigin.x + popupWindowSize.x + gap,
                                            center.y - signalSearchPlotSize.y * 0.5f);
                        signalSearchPlotPos = clampPos(signalSearchPlotPos, signalSearchPlotSize);
                        childFocused = signalSearchPlot(msg.signals[idx], msg.time, signalSearchPlotPos, &signalSearchPlotSize);
                    }
                }
            ImGui::EndListBox();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    return (focused || childFocused);
}

bool UI::signalSearchPlot(const CanSignal& sig, const std::vector<double>& time, ImVec2 pos, ImVec2* outSize){
    bool focused = false;
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | 
                             ImGuiWindowFlags_NoSavedSettings  | 
                             ImGuiWindowFlags_NoTitleBar | 
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
    ImGui::SetNextWindowBgAlpha(0.90f);
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

void UI::videoWindow(){
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
}

void UI::setStyle(){
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowBorderSize = 0.0f;
    style.DockingSeparatorSize = 1.0f;
    style.WindowPadding = ImVec2{8.0f, 5.0f};

    ImVec4* colors = style.Colors;
    const ImVec4 almostBlack{0.05f, 0.05f, 0.05f, 1.00f};
    const ImVec4 darkGray{0.10f, 0.10f, 0.10f, 1.00f};
    const ImVec4 midGray{0.15f, 0.15f, 0.15f, 1.00f};
    const ImVec4 lightGray{0.25f, 0.25f, 0.25f, 1.00f};
    const ImVec4 lightBlue{0.00f, 0.75f, 0.75f, 1.00f};
    const ImVec4 textColor{1.00f, 1.00f, 1.00f, 1.00f};

    colors[ImGuiCol_Text] = textColor;
    colors[ImGuiCol_TextDisabled] = ImVec4{0.50f, 0.50f, 0.50f, 1.00f};
    colors[ImGuiCol_WindowBg] = ImVec4{0.05f, 0.05f, 0.05f, 1.00f};
    colors[ImGuiCol_ChildBg] = darkGray;
    colors[ImGuiCol_PopupBg] = almostBlack;
    colors[ImGuiCol_Border] = ImVec4{0.32f, 0.32f, 0.32f, 1.00f};
    colors[ImGuiCol_FrameBg] = ImVec4{0.18f, 0.18f, 0.18f, 1.00f};
    colors[ImGuiCol_FrameBgHovered] = ImVec4{0.28f, 0.28f, 0.28f, 1.00f};
    colors[ImGuiCol_FrameBgActive] = ImVec4{0.34f, 0.34f, 0.34f, 1.00f};
    colors[ImGuiCol_TitleBg] = darkGray;
    colors[ImGuiCol_TitleBgActive] = midGray;
    colors[ImGuiCol_TitleBgCollapsed] = almostBlack;
    colors[ImGuiCol_MenuBarBg] = darkGray;
    colors[ImGuiCol_ScrollbarBg] = darkGray;
    colors[ImGuiCol_ScrollbarGrab] = midGray;
    colors[ImGuiCol_ScrollbarGrabHovered] = lightGray;
    colors[ImGuiCol_ScrollbarGrabActive] = lightGray;
    colors[ImGuiCol_CheckMark] = textColor;
    colors[ImGuiCol_SliderGrab] = ImVec4{0.40f, 0.40f, 0.40f, 1.00f};
    colors[ImGuiCol_SliderGrabActive] = ImVec4{0.30f, 0.30f, 0.30f, 1.00f};
    colors[ImGuiCol_Button] = lightGray;
    colors[ImGuiCol_ButtonHovered] = lightGray;
    colors[ImGuiCol_ButtonActive] = lightGray;
    colors[ImGuiCol_Header] = midGray;
    colors[ImGuiCol_HeaderHovered] = lightGray;
    colors[ImGuiCol_HeaderActive] = lightGray;
    colors[ImGuiCol_Separator] = midGray;
    colors[ImGuiCol_SeparatorHovered] = lightGray;
    colors[ImGuiCol_SeparatorActive] = lightGray;
    colors[ImGuiCol_ResizeGrip] = midGray;
    colors[ImGuiCol_ResizeGripHovered] = lightGray;
    colors[ImGuiCol_ResizeGripActive] = lightGray;
    colors[ImGuiCol_Tab] = midGray;
    colors[ImGuiCol_TabHovered] = lightGray;
    colors[ImGuiCol_TabActive] = lightGray;
    colors[ImGuiCol_TabUnfocused] = darkGray;
    colors[ImGuiCol_TabUnfocusedActive] = midGray;
    colors[ImGuiCol_TabSelectedOverline] = ImVec4{0.00f, 0.00f, 0.00f, 0.00f};
    colors[ImGuiCol_DockingPreview] = lightBlue;
    colors[ImGuiCol_DockingEmptyBg] = ImVec4{0.00f, 0.00f, 0.00f, 0.00f};
    colors[ImGuiCol_PlotLines] = ImVec4{0.61f, 0.61f, 0.61f, 1.00f};
    colors[ImGuiCol_PlotLinesHovered] = lightGray;
    colors[ImGuiCol_PlotHistogram] = midGray;
    colors[ImGuiCol_PlotHistogramHovered] = lightGray;
    colors[ImGuiCol_TextSelectedBg] = midGray;
    colors[ImGuiCol_DragDropTarget] = lightGray;
    colors[ImGuiCol_NavHighlight] = lightGray;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};
    colors[ImGuiCol_ModalWindowDimBg] = almostBlack;

    ImPlotStyle &plotStyle = ImPlot::GetStyle();
    plotStyle.Colors[ImPlotCol_FrameBg] = midGray;
    //plotStyle.Colors[ImPlotCol_PlotBg] = ImVec4(0, 0, 0, 0.95f);
    //plotStyle.Colors[ImPlotCol_PlotBorder] = ImVec4(0, 0, 0, 0.0f);
    //plotStyle.Colors[ImPlotCol_LegendBg] = ImVec4(0, 0, 0, 0.0f);
    //plotStyle.Colors[ImPlotCol_LegendBorder] = ImVec4(0, 0, 0, 0.0f);

}
