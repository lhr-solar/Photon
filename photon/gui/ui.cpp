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
#include <unordered_set>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <commdlg.h>
#endif
#include "console.hpp"
#include "imgui_internal.h"
#include "implot.h"
#include "implot3d.h"
#include "imnodes.h"

namespace {
constexpr const char* kCustomDbcPrefix = "custom-file:";

bool isCustomDbcSelection(const std::string& selection) {
    return selection.rfind(kCustomDbcPrefix, 0) == 0;
}

std::string makeCustomDbcSelection(const std::string& filePath) {
    return std::string(kCustomDbcPrefix) + filePath;
}

std::string customDbcPathFromSelection(const std::string& selection) {
    if (!isCustomDbcSelection(selection)) {
        return {};
    }
    return selection.substr(std::strlen(kCustomDbcPrefix));
}

int dbcSelectionIndexFromValue(const std::string& selection) {
    if (selection == "assettoCorsa") { return 0; }
    if (selection == "daybreak") { return 1; }
    if (selection == "vehicle-with-undisclosed-name") { return 2; }
    if (isCustomDbcSelection(selection)) { return 3; }
    return 0;
}

void copyStringToBuffer(const std::string& value, char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return;
    }
    std::snprintf(buffer, bufferSize, "%s", value.c_str());
}

std::string trimWhitespace(std::string text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

#ifndef _WIN32
std::string runFilePickerCommand(const char* command) {
    FILE* pipe = popen(command, "r");
    if (pipe == nullptr) {
        return {};
    }

    char buffer[1024];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    const int status = pclose(pipe);
    if (status != 0) {
        return {};
    }
    return trimWhitespace(output);
}
#endif

std::string openDbcFilePicker() {
#ifdef _WIN32
    char fileBuffer[MAX_PATH] = {0};
    OPENFILENAMEA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = "DBC Files\0*.dbc;*.DBC\0All Files\0*.*\0";
    dialog.lpstrFile = fileBuffer;
    dialog.nMaxFile = static_cast<DWORD>(sizeof(fileBuffer));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = "Select DBC File";
    if (GetOpenFileNameA(&dialog) == TRUE) {
        return fileBuffer;
    }
    return {};
#else
    std::string path = runFilePickerCommand("command -v zenity >/dev/null 2>&1 && zenity --file-selection --title='Select DBC File' --file-filter='DBC files | *.dbc *.DBC' --file-filter='All files | *' 2>/dev/null");
    if (!path.empty()) {
        return path;
    }
    path = runFilePickerCommand("command -v kdialog >/dev/null 2>&1 && kdialog --getopenfilename . '*.dbc *.DBC|DBC files' 2>/dev/null");
    return path;
#endif
}

VkExtent2D quantizeContentExtent(ImVec2 contentSize, VkExtent2D fallback) {
    if (contentSize.x <= 1.0f || contentSize.y <= 1.0f) {
        return fallback;
    }

    const uint32_t width = std::max(1u, static_cast<uint32_t>(std::lround(contentSize.x)));
    const uint32_t height = std::max(1u, static_cast<uint32_t>(std::lround(contentSize.y)));
    return {width, height};
}

int64_t parseSignedTextOrDefault(const char* text, int64_t fallback) {
    if (text == nullptr || text[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const long long value = std::strtoll(text, &end, 10);
    if (end == text) {
        return fallback;
    }
    return static_cast<int64_t>(value);
}

uint32_t parseUnsignedTextOrDefault(const char* text, uint32_t fallback) {
    if (text == nullptr || text[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text) {
        return fallback;
    }
    return static_cast<uint32_t>(value);
}

double parseDoubleTextOrDefault(const char* text, double fallback) {
    if (text == nullptr || text[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text) {
        return fallback;
    }
    return value;
}

uint8_t parseHexByteOrDefault(const char* text, uint8_t fallback) {
    if (text == nullptr || text[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 16);
    if (end == text) {
        return fallback;
    }
    return static_cast<uint8_t>(value & 0xFFu);
}

constexpr int kControlNodeId = 5;
constexpr int kSinNodeId = 6;
constexpr int kDataNodeId = 8;
constexpr int kPlotNodeId = 10;
constexpr int kProceduralStoreNodeId = 1000;
constexpr int kProceduralStoreOutAttrId = 1001;
constexpr int kProceduralMessageNodeBase = 10000;
constexpr int kProceduralSignalNodeBase = 1000000;
constexpr int kProceduralStoreLinkBase = 2000000;
constexpr int kProceduralSignalLinkBase = 3000000;

int proceduralMessageNodeId(int canId) {
    return kProceduralMessageNodeBase + (canId * 8);
}

int proceduralMessageInAttrId(int canId) {
    return proceduralMessageNodeId(canId) + 1;
}

int proceduralMessageOutAttrId(int canId) {
    return proceduralMessageNodeId(canId) + 2;
}

int proceduralMessageDataInAttrId(int canId) {
    return proceduralMessageNodeId(canId) + 3;
}

int proceduralSignalNodeId(int canId, int signalIndex) {
    return kProceduralSignalNodeBase + (canId * 256) + (signalIndex * 2);
}

int proceduralSignalInAttrId(int canId, int signalIndex) {
    return proceduralSignalNodeId(canId, signalIndex) + 1;
}

int proceduralSignalOutAttrId(int canId, int signalIndex) {
    return proceduralSignalNodeId(canId, signalIndex) + 2;
}

int proceduralStoreLinkId(int canId) {
    return kProceduralStoreLinkBase + canId;
}

int proceduralSignalLinkId(int canId, int signalIndex) {
    return kProceduralSignalLinkBase + (canId * 256) + signalIndex;
}

#ifdef _WIN32
using TPCANHandle = WORD;
using TPCANStatus = DWORD;
using TPCANParameter = BYTE;

constexpr TPCANStatus kPcanErrorOk = 0x00000U;
constexpr TPCANParameter kPcanChannelCondition = 0x0DU;
constexpr DWORD kPcanChannelAvailable = 0x01U;
constexpr DWORD kPcanChannelOccupied = 0x02U;

using CanGetValueFn = TPCANStatus (__stdcall *)(TPCANHandle, TPCANParameter, void*, DWORD);

struct PcanChannelInfo {
    TPCANHandle handle;
    const char* name;
};

constexpr std::array<PcanChannelInfo, 16> kPcanUsbChannels = {{
    {0x51U, "PCAN_USBBUS1"},
    {0x52U, "PCAN_USBBUS2"},
    {0x53U, "PCAN_USBBUS3"},
    {0x54U, "PCAN_USBBUS4"},
    {0x55U, "PCAN_USBBUS5"},
    {0x56U, "PCAN_USBBUS6"},
    {0x57U, "PCAN_USBBUS7"},
    {0x58U, "PCAN_USBBUS8"},
    {0x509U, "PCAN_USBBUS9"},
    {0x50AU, "PCAN_USBBUS10"},
    {0x50BU, "PCAN_USBBUS11"},
    {0x50CU, "PCAN_USBBUS12"},
    {0x50DU, "PCAN_USBBUS13"},
    {0x50EU, "PCAN_USBBUS14"},
    {0x50FU, "PCAN_USBBUS15"},
    {0x510U, "PCAN_USBBUS16"},
}};

std::vector<std::string> enumerateWindowsPcanChannels() {
    std::vector<std::string> channels;
    HMODULE library = LoadLibraryA("PCANBasic.dll");
    if(library == nullptr){
        return channels;
    }

    const auto canGetValue = reinterpret_cast<CanGetValueFn>(GetProcAddress(library, "CAN_GetValue"));
    if(canGetValue == nullptr){
        FreeLibrary(library);
        return channels;
    }

    for(const PcanChannelInfo& channel : kPcanUsbChannels){
        DWORD condition = 0;
        const TPCANStatus status = canGetValue(channel.handle, kPcanChannelCondition, &condition, sizeof(condition));
        if(status != kPcanErrorOk){
            continue;
        }
        if((condition & (kPcanChannelAvailable | kPcanChannelOccupied)) != 0){
            channels.emplace_back(channel.name);
        }
    }

    FreeLibrary(library);
    return channels;
}
#endif

}

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

struct MessageOption {
    int canId = -1;
    std::string label;
};

struct SourceMatch {
    int optionIndex = -1;
    int score = 0;
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
    std::string title;
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
    size_t sourceMatchesSignature = std::numeric_limits<size_t>::max();
    std::string sourceMatchesQuery;
    std::vector<SourceMatch> sourceMatches;
    int nextId = 1;
    std::vector<GeneratedPlotWindow> windows;
};

void refreshGeneratedPlotWindowTitle(GeneratedPlotWindow& plot);

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
    refreshGeneratedPlotWindowTitle(window);

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

const char* threeDSourceLabel(size_t sourceIndex, bool useSource1TimeAsX) {
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

void refreshGeneratedPlotWindowTitle(GeneratedPlotWindow& plot) {
    plot.title = generatedPlotWindowTitle(plot);
}

const std::string& windowTitleFor(GeneratedPlotWindow& plot) {
    if (plot.title.empty()) {
        refreshGeneratedPlotWindowTitle(plot);
    }
    return plot.title;
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

uint64_t sourceRefKey(int canId, int signalIndex) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(canId)) << 32u) |
           static_cast<uint32_t>(signalIndex);
}

uint64_t sourceRefKey(const PlotDataSourceRef& src) {
    return sourceRefKey(src.canId, src.signalIndex);
}

struct SignalOptionsCache {
    Parse* parseInterface = nullptr;
    size_t signature = 0;
    std::vector<SignalOption> options;
    std::unordered_map<uint64_t, int> optionIndexByRef;
};

struct MessageOptionsCache {
    Parse* parseInterface = nullptr;
    size_t signature = 0;
    std::vector<MessageOption> options;
};

SignalOptionsCache& signalOptionsCache() {
    static SignalOptionsCache cache;
    return cache;
}

MessageOptionsCache& messageOptionsCache() {
    static MessageOptionsCache cache;
    return cache;
}

size_t computeSignalOptionsSignature(Parse* parseInterface) {
    if (!parseInterface) { return 0; }
    size_t sig = parseInterface->canStore.canMessages.size();
    for (const auto& [canId, msg] : parseInterface->canStore.canMessages) {
        sig ^= (std::hash<int>{}(canId) + 0x9e3779b97f4a7c15ull + (sig << 6u) + (sig >> 2u));
        sig ^= (std::hash<size_t>{}(msg.signals.size()) + 0x9e3779b97f4a7c15ull + (sig << 6u) + (sig >> 2u));
    }
    return sig;
}

size_t computeMessageOptionsSignature(Parse* parseInterface) {
    if (!parseInterface) { return 0; }
    size_t sig = parseInterface->canStore.canMessages.size();
    for (const auto& [canId, msg] : parseInterface->canStore.canMessages) {
        sig ^= (std::hash<int>{}(canId) + 0x9e3779b97f4a7c15ull + (sig << 6u) + (sig >> 2u));
        sig ^= (std::hash<std::string>{}(msg.name) + 0x9e3779b97f4a7c15ull + (sig << 6u) + (sig >> 2u));
    }
    return sig;
}

void rebuildSignalOptionsCache(Parse* parseInterface, size_t signature) {
    SignalOptionsCache& cache = signalOptionsCache();
    cache.options.clear();
    cache.optionIndexByRef.clear();
    cache.parseInterface = parseInterface;
    cache.signature = signature;
    if (!parseInterface) { return; }

    cache.options.reserve(parseInterface->canStore.canMessages.size() * 8);
    cache.optionIndexByRef.reserve(parseInterface->canStore.canMessages.size() * 8);

    for (const auto& [canId, msg] : parseInterface->canStore.canMessages) {
        char idBuf[16] = {0};
        std::snprintf(idBuf, sizeof(idBuf), "0x%03X", canId);
        for (size_t i = 0; i < msg.signals.size(); ++i) {
            SignalOption opt;
            opt.ref.canId = canId;
            opt.ref.signalIndex = static_cast<int>(i);
            opt.label.reserve(16 + msg.name.size() + msg.signals[i].name.size());
            opt.label.append(idBuf).append(" : ").append(msg.name).append(" / ").append(msg.signals[i].name);
            const int optionIndex = static_cast<int>(cache.options.size());
            cache.optionIndexByRef.emplace(sourceRefKey(opt.ref), optionIndex);
            cache.options.push_back(std::move(opt));
        }
    }
}

void rebuildMessageOptionsCache(Parse* parseInterface, size_t signature) {
    MessageOptionsCache& cache = messageOptionsCache();
    cache.options.clear();
    cache.parseInterface = parseInterface;
    cache.signature = signature;
    if (!parseInterface) { return; }

    cache.options.reserve(parseInterface->canStore.canMessages.size());
    for (const auto& [canId, msg] : parseInterface->canStore.canMessages) {
        MessageOption opt;
        opt.canId = canId;
        char idBuf[16] = {0};
        std::snprintf(idBuf, sizeof(idBuf), "0x%03X", canId);
        opt.label.reserve(16 + msg.name.size());
        opt.label.append(idBuf).append("  ").append(msg.name);
        cache.options.push_back(std::move(opt));
    }
}

const SignalOptionsCache& getSignalOptionsCache(Parse* parseInterface) {
    SignalOptionsCache& cache = signalOptionsCache();
    const size_t signature = computeSignalOptionsSignature(parseInterface);
    if (cache.parseInterface != parseInterface || cache.signature != signature) {
        rebuildSignalOptionsCache(parseInterface, signature);
    }
    return cache;
}

const MessageOptionsCache& getMessageOptionsCache(Parse* parseInterface) {
    MessageOptionsCache& cache = messageOptionsCache();
    const size_t signature = computeMessageOptionsSignature(parseInterface);
    if (cache.parseInterface != parseInterface || cache.signature != signature) {
        rebuildMessageOptionsCache(parseInterface, signature);
    }
    return cache;
}

const std::vector<SignalOption>& collectSignalOptions(Parse* parseInterface) {
    return getSignalOptionsCache(parseInterface).options;
}

const CanMessage* findMessage(Parse* parseInterface, int canId) {
    if (!parseInterface) { return nullptr; }
    auto it = parseInterface->canStore.canMessages.find(canId);
    return (it == parseInterface->canStore.canMessages.end()) ? nullptr : &it->second;
}

const CanSignal* findSignal(Parse* parseInterface, const PlotDataSourceRef& src) {
    const CanMessage* msg = findMessage(parseInterface, src.canId);
    if (!msg) { return nullptr; }
    if (src.signalIndex < 0 || static_cast<size_t>(src.signalIndex) >= msg->signals.size()) { return nullptr; }
    return &msg->signals[static_cast<size_t>(src.signalIndex)];
}

std::string sourceName(Parse* parseInterface, const PlotDataSourceRef& src) {
    const CanMessage* msg = findMessage(parseInterface, src.canId);
    if (!msg) { return "<missing message>"; }
    if (src.signalIndex < 0 || static_cast<size_t>(src.signalIndex) >= msg->signals.size()) { return "<missing signal>"; }
    char idBuf[16] = {0};
    std::snprintf(idBuf, sizeof(idBuf), "0x%03X", src.canId);
    return std::string(idBuf) + "/" + msg->signals[static_cast<size_t>(src.signalIndex)].name;
}

int findOptionIndex(const SignalOptionsCache& cache, const PlotDataSourceRef& src) {
    auto it = cache.optionIndexByRef.find(sourceRefKey(src));
    return (it == cache.optionIndexByRef.end()) ? -1 : it->second;
}

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

struct SourceMatchLess {
    const std::vector<SignalOption>* options = nullptr;
    bool operator()(const SourceMatch& a, const SourceMatch& b) const {
        if (a.score != b.score) { return a.score < b.score; }
        return (*options)[static_cast<size_t>(a.optionIndex)].label <
               (*options)[static_cast<size_t>(b.optionIndex)].label;
    }
};

void buildSourceMatches(const std::vector<SignalOption>& options, const char* query, std::vector<SourceMatch>& matches) {
    matches.clear();
    matches.reserve(options.size());

    const bool hasQuery = (query != nullptr && query[0] != '\0');
    for (size_t i = 0; i < options.size(); ++i) {
        SourceMatch m;
        m.optionIndex = static_cast<int>(i);
        m.score = hasQuery ? distance(query, options[i].label) : 0;
        matches.push_back(m);
    }

    SourceMatchLess less;
    less.options = &options;
    std::sort(matches.begin(), matches.end(), less);
}

void buildMessageMatches(const std::vector<MessageOption>& options, const char* query, std::vector<SourceMatch>& matches) {
    matches.clear();
    matches.reserve(options.size());

    const bool hasQuery = (query != nullptr && query[0] != '\0');
    for (size_t i = 0; i < options.size(); ++i) {
        SourceMatch m;
        m.optionIndex = static_cast<int>(i);
        m.score = hasQuery ? distance(query, options[i].label) : 0;
        matches.push_back(m);
    }

    std::sort(matches.begin(), matches.end(), [&options](const SourceMatch& a, const SourceMatch& b) {
        if (a.score != b.score) { return a.score < b.score; }
        return options[static_cast<size_t>(a.optionIndex)].label <
               options[static_cast<size_t>(b.optionIndex)].label;
    });
}

struct PlotScratchBuffers {
    std::vector<const CanSignal*> signals;
    std::vector<double> values;
    std::vector<const char*> labels;
};

bool generatedPlotWindowClosed(const GeneratedPlotWindow& window) {
    return !window.open;
}

void pruneListWindowStatesForActiveWindows(const std::vector<GeneratedPlotWindow>& windows) {
    auto& listStates = listWindowStates();
    std::unordered_set<int> activeIds;
    activeIds.reserve(windows.size());
    for (const GeneratedPlotWindow& window : windows) {
        activeIds.insert(window.id);
    }
    for (auto it = listStates.begin(); it != listStates.end();) {
        if (activeIds.find(it->first) == activeIds.end()) {
            it = listStates.erase(it);
        } else {
            ++it;
        }
    }
}

int clampByBudget(int wantedCost, int units, int minUnits, int costPerUnit, int& frameBudget) {
    if (units <= 0 || costPerUnit <= 0) { return 0; }
    const int allowed = std::max(minUnits, std::min(units, frameBudget / costPerUnit));
    const int actualCost = std::min(wantedCost, allowed * costPerUnit);
    frameBudget = std::max(0, frameBudget - actualCost);
    return allowed;
}

void plotSettingsClearAll(ImGuiContext*, ImGuiSettingsHandler*) {
    clearPlotSettings();
}

int parseComIndexFromName(const std::string& name) {
    if (name.size() <= 3) { return 0; }
    return std::atoi(name.c_str() + 3);
}

bool lessComPortName(const std::string& lhs, const std::string& rhs) {
    const int leftIdx = parseComIndexFromName(lhs);
    const int rightIdx = parseComIndexFromName(rhs);
    if (leftIdx == rightIdx) { return lhs < rhs; }
    return leftIdx < rightIdx;
}

#ifdef _WIN32
std::vector<std::string> enumerateWindowsSerialPorts() {
    std::vector<std::string> ports;

    HKEY key = nullptr;
    const LSTATUS openStatus = RegOpenKeyExA(
        HKEY_LOCAL_MACHINE,
        "HARDWARE\\DEVICEMAP\\SERIALCOMM",
        0,
        KEY_QUERY_VALUE,
        &key);
    if(openStatus != ERROR_SUCCESS){
        return ports;
    }

    DWORD valueCount = 0;
    DWORD maxValueNameLen = 0;
    DWORD maxValueLen = 0;
    const LSTATUS queryStatus = RegQueryInfoKeyA(
        key,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &valueCount,
        &maxValueNameLen,
        &maxValueLen,
        nullptr,
        nullptr);
    if(queryStatus != ERROR_SUCCESS){
        RegCloseKey(key);
        return ports;
    }

    std::vector<char> valueName(maxValueNameLen + 1, '\0');
    std::vector<std::byte> valueData(maxValueLen + sizeof(char16_t), std::byte{0});
    for(DWORD index = 0; index < valueCount; ++index){
        DWORD nameLen = maxValueNameLen + 1;
        DWORD dataLen = maxValueLen;
        DWORD type = 0;
        const LSTATUS enumStatus = RegEnumValueA(
            key,
            index,
            valueName.data(),
            &nameLen,
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(valueData.data()),
            &dataLen);
        if(enumStatus != ERROR_SUCCESS){
            continue;
        }

        if(type == REG_SZ && dataLen > 0){
            const char* portName = reinterpret_cast<const char*>(valueData.data());
            if(*portName != '\0'){
                ports.emplace_back(portName);
            }
        }
    }

    RegCloseKey(key);
    std::sort(ports.begin(), ports.end(), lessComPortName);
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    return ports;
}
#endif

ImVec2 clampPosToViewport(const ImGuiViewport* vp, const ImVec2& pos, const ImVec2& size) {
    const float minX = vp->Pos.x + 8.0f;
    const float minY = vp->Pos.y + 8.0f;
    const float maxX = vp->Pos.x + vp->Size.x - size.x - 8.0f;
    const float maxY = vp->Pos.y + vp->Size.y - size.y - 8.0f;
    return ImVec2(std::clamp(pos.x, minX, std::max(minX, maxX)),
                  std::clamp(pos.y, minY, std::max(minY, maxY)));
}

bool lessCmdResult(const UI::CmdResult& a, const UI::CmdResult& b) {
    if (a.distance != b.distance) { return a.distance < b.distance; }
    return a.name < b.name;
}

PlotScratchBuffers& plotScratchBuffers() {
    static thread_local PlotScratchBuffers scratch;
    return scratch;
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

void renderTimeSeriesPlot(Parse* parseInterface, GeneratedPlotWindow& plot) {
    if (plot.sources.empty()) {
        ImGui::TextUnformatted("Missing data sources.");
        return;
    }
    const CanMessage* primaryMsg = findMessage(parseInterface, plot.sources[0].canId);
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
    PlotScratchBuffers& scratch = plotScratchBuffers();
    std::vector<const CanSignal*>& signals = scratch.signals;
    signals.clear();
    signals.reserve(plot.sources.size());
    for (const PlotDataSourceRef& src : plot.sources) {
        const CanSignal* signal = findSignal(parseInterface, src);
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

    char plotId[64] = {0};
    std::snprintf(plotId, sizeof(plotId), "##generatedPlot_%d", plot.id);
    if (!ImPlot::BeginPlot(plotId, ImVec2(-FLT_MIN, -FLT_MIN))) {
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
                std::vector<double>& values = scratch.values;
                std::vector<const char*>& labels = scratch.labels;
                values.assign(static_cast<size_t>(itemCount * groupCount), 0.0);
                labels.resize(static_cast<size_t>(itemCount));
                for (int item = 0; item < itemCount; ++item) {
                    labels[static_cast<size_t>(item)] = signals[static_cast<size_t>(item)]->name.c_str();
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

void renderNonTimePlot(Parse* parseInterface, GeneratedPlotWindow& plot) {
    PlotScratchBuffers& scratch = plotScratchBuffers();
    std::vector<const CanSignal*>& signals = scratch.signals;
    signals.clear();
    signals.reserve(plot.sources.size());
    for (const PlotDataSourceRef& src : plot.sources) {
        const CanSignal* signal = findSignal(parseInterface, src);
        if (signal) { signals.push_back(signal); }
    }
    if (signals.empty()) {
        ImGui::TextUnformatted("No valid data sources for this plot.");
        return;
    }

    char plotId[64] = {0};
    std::snprintf(plotId, sizeof(plotId), "##generatedPlot_%d", plot.id);
    if (!ImPlot::BeginPlot(plotId, ImVec2(-FLT_MIN, -FLT_MIN))) {
        return;
    }

    switch (plot.typeIndex) {
        case PlotType_Pie: {
            std::vector<double>& values = scratch.values;
            std::vector<const char*>& labels = scratch.labels;
            values.resize(signals.size());
            labels.resize(signals.size());
            for (size_t i = 0; i < signals.size(); ++i) {
                const auto& data = signals[i]->data;
                values[i] = data.empty() ? 0.0 : data.back();
                labels[i] = signals[i]->name.c_str();
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

void render3DPlot(Parse* parseInterface, GeneratedPlotWindow& plot) {
    static int lastFrame = -1;
    static int frameBudget = 12000;
    const int frameNow = ImGui::GetFrameCount();
    if (frameNow != lastFrame) {
        lastFrame = frameNow;
        frameBudget = 12000;
    }
    char plotId[64] = {0};
    std::snprintf(plotId, sizeof(plotId), "##generatedPlot3D_%d", plot.id);
    if (!ImPlot3D::BeginPlot(plotId, ImVec2(-FLT_MIN, -FLT_MIN), ImPlot3DFlags_None)) {
        return;
    }

    if (plot.typeIndex == PlotType_3DLine) {
        const size_t requiredSources = static_cast<size_t>(required3DSources(plot.useSource1TimeAsX));
        if (plot.sources.size() < requiredSources) {
            ImPlot3D::EndPlot();
            ImGui::TextUnformatted(threeDRequiredSourcesText(plot.typeIndex, plot.useSource1TimeAsX));
            return;
        }
        const CanMessage* timeMsg = findMessage(parseInterface, plot.sources[0].canId);
        const CanSignal* source1 = findSignal(parseInterface, plot.sources[0]);
        const CanSignal* source2 = findSignal(parseInterface, plot.sources[1]);
        const CanSignal* source3 = plot.useSource1TimeAsX ? nullptr : findSignal(parseInterface, plot.sources[2]);
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
        const int allowed = clampByBudget(slice.count * 2, slice.count, 2, 2, frameBudget);

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
        PlotScratchBuffers& scratch = plotScratchBuffers();
        std::vector<const CanSignal*>& signals = scratch.signals;
        signals.clear();
        signals.reserve(plot.sources.size());
        for (const PlotDataSourceRef& src : plot.sources) {
            const CanSignal* signal = findSignal(parseInterface, src);
            if (signal) { signals.push_back(signal); }
        }
        if (signals.size() < requiredSources) {
            ImPlot3D::EndPlot();
            ImGui::TextUnformatted(threeDRequiredSourcesText(plot.typeIndex, plot.useSource1TimeAsX));
            return;
        }
        const CanMessage* timeMsg = plot.useSource1TimeAsX ? findMessage(parseInterface, plot.sources[0].canId) : nullptr;
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
        const int allowed = clampByBudget(slice.count * 12, slice.count, 2, 12, frameBudget);
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
        int allowedPointCount = clampByBudget(pointCount * 8, pointCount, 4, 8, frameBudget);
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

void renderListWindow(Parse* parseInterface, GeneratedPlotWindow& plot, const std::vector<SignalOption>& options, bool allowAdd) {
    if (ImGui::BeginTable("##signalList", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableSetupColumn("Latest", ImGuiTableColumnFlags_WidthStretch, 0.3f);
        ImGui::TableHeadersRow();

        for (const PlotDataSourceRef& src : plot.sources) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const CanMessage* msg = findMessage(parseInterface, src.canId);
            if (!msg) {
                ImGui::TextUnformatted("<missing message>");
            } else if (src.signalIndex < 0 || static_cast<size_t>(src.signalIndex) >= msg->signals.size()) {
                ImGui::TextUnformatted("<missing signal>");
            } else {
                char sourceBuf[128] = {0};
                std::snprintf(sourceBuf, sizeof(sourceBuf), "0x%03X/%s", src.canId, msg->signals[static_cast<size_t>(src.signalIndex)].name.c_str());
                ImGui::TextUnformatted(sourceBuf);
            }

            ImGui::TableSetColumnIndex(1);
            const CanSignal* signal = findSignal(parseInterface, src);
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

void drawGeneratedPlots(Parse* parseInterface, ImGuiID customDockspaceId, bool customVisible) {
    PlotGeneratorState& state = generatorState();
    const std::vector<SignalOption>* options = nullptr;
    const size_t countBefore = state.windows.size();

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
            const std::string& windowTitle = windowTitleFor(plot);
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
            const std::string& windowTitle = windowTitleFor(plot);
            ImGuiWindow* existing = ImGui::FindWindowByName(windowTitle.c_str());
            const bool isUndocked = (existing != nullptr) && (existing->DockNode == nullptr);
            if (isUndocked) {
                plot.requestRedock = true;
            }
        }
        state.windows.erase(std::remove_if(state.windows.begin(), state.windows.end(),
            generatedPlotWindowClosed),
            state.windows.end());
        if (state.windows.size() != countBefore) {
            ImGui::MarkIniSettingsDirty();
            persistIniNowIfAvailable();
        }
        pruneListWindowStatesForActiveWindows(state.windows);
        return;
    }

    for (GeneratedPlotWindow& plot : state.windows) {
        const std::string& windowTitle = windowTitleFor(plot);
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
                render3DPlot(parseInterface, plot);
            } else if (plot.typeIndex == PlotType_List) {
                if (options == nullptr) {
                    options = &getSignalOptionsCache(parseInterface).options;
                }
                renderListWindow(parseInterface, plot, *options, true);
            } else if (specFor(plot.typeIndex).usesTimeAxis) {
                renderTimeSeriesPlot(parseInterface, plot);
            } else {
                renderNonTimePlot(parseInterface, plot);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleVar();
    }
    state.windows.erase(std::remove_if(state.windows.begin(), state.windows.end(),
        generatedPlotWindowClosed),
        state.windows.end());
    if (state.windows.size() != countBefore) {
        ImGui::MarkIniSettingsDirty();
        persistIniNowIfAvailable();
    }
    pruneListWindowStatesForActiveWindows(state.windows);
}

void UI::drawGeneratorUI() {
    PlotGeneratorState& state = generatorState();

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
    const SignalOptionsCache& optionCache = getSignalOptionsCache(parseInterface);
    const std::vector<SignalOption>& options = optionCache.options;
    std::vector<SourceMatch> previewMatches;
    const std::vector<SourceMatch>* widthMatches = &state.sourceMatches;
    const size_t optionsSignature = optionCache.signature;
    if (state.sourceMatchesSignature != optionsSignature || state.sourceMatchesQuery != state.sourceQuery) {
        buildSourceMatches(options, state.sourceQuery, previewMatches);
        widthMatches = &previewMatches;
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 center = vp ? vp->GetCenter() : ImVec2(0, 0);
    const ImGuiStyle& style = ImGui::GetStyle();
    float maxSuggestionWidth = 0.0f;
    for (const SourceMatch& match : *widthMatches) {
        if (match.optionIndex < 0 || match.optionIndex >= static_cast<int>(options.size())) { continue; }
        maxSuggestionWidth = std::max(maxSuggestionWidth,
                                      ImGui::CalcTextSize(options[static_cast<size_t>(match.optionIndex)].label.c_str()).x);
    }
    const float previewColumnWidth = 340.0f;
    const float assignedColumnWidth = 340.0f;
    const float minSearchColumnWidth = 320.0f;
    const float desiredSearchColumnWidth = std::max(
        minSearchColumnWidth,
        maxSuggestionWidth + style.FramePadding.x * 2.0f + style.CellPadding.x * 2.0f + style.ScrollbarSize + 24.0f);
    const float availableViewportWidth = vp ? vp->WorkSize.x : io.DisplaySize.x;
    const float maxWindowWidth = std::max(720.0f, availableViewportWidth - 48.0f);
    const float desiredWindowWidth = std::min(
        maxWindowWidth,
        previewColumnWidth + assignedColumnWidth + desiredSearchColumnWidth +
            style.WindowPadding.x * 2.0f + style.CellPadding.x * 6.0f + style.ItemSpacing.x * 2.0f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(desiredWindowWidth, 0.0f), ImGuiCond_Always);
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
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, previewColumnWidth);
        ImGui::TableSetupColumn("Assigned", ImGuiTableColumnFlags_WidthStretch, assignedColumnWidth);
        ImGui::TableSetupColumn("Search", ImGuiTableColumnFlags_WidthStretch, desiredSearchColumnWidth);
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
                render3DPlot(parseInterface, previewPlot);
            } else if (previewPlot.typeIndex == PlotType_List) {
                renderListWindow(parseInterface, previewPlot, options, false);
            } else if (specFor(previewPlot.typeIndex).usesTimeAxis) {
                renderTimeSeriesPlot(parseInterface, previewPlot);
            } else {
                renderNonTimePlot(parseInterface, previewPlot);
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
            int selectedIndex = findOptionIndex(optionCache, state.sources[i]);
            const char* sourceLabel = (selectedIndex >= 0) ? options[static_cast<size_t>(selectedIndex)].label.c_str() : "<unassigned>";
            char lineLabel[64] = {0};
            if (spec.is3D) {
                std::snprintf(lineLabel, sizeof(lineLabel), "%s", threeDSourceLabel(i, state.useSource1TimeAsX));
            } else {
                std::snprintf(lineLabel, sizeof(lineLabel), "Signal %zu", i + 1);
            }
            char rowText[640] = {0};
            std::snprintf(rowText, sizeof(rowText), "%s: %s", lineLabel, sourceLabel);
            if (ImGui::Selectable(rowText, selectedRow, ImGuiSelectableFlags_AllowDoubleClick)) {
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

            if (state.sourceMatchesSignature != optionsSignature || state.sourceMatchesQuery != state.sourceQuery) {
                buildSourceMatches(options, state.sourceQuery, state.sourceMatches);
                state.sourceMatchesSignature = optionsSignature;
                state.sourceMatchesQuery = state.sourceQuery;
            }
            const std::vector<SourceMatch>& matches = state.sourceMatches;
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
        if (findOptionIndex(optionCache, src) < 0) {
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
        refreshGeneratedPlotWindowTitle(window);
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
    handler.ClearAllFn = plotSettingsClearAll;
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
    ImGui::NewFrame();
    setScale();

    background();
    for (auto& [id, msg] : parseInterface->canStore.canMessages) msg.updateMessage(parseInterface);

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags root_flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);

    ImGuiID rootDockId = ImGui::GetID("RootDock");
    if (ImGui::Begin("Root Dock##RootDock", nullptr, root_flags)) {
        const ImGuiDockNodeFlags lockedTabsOnlyFlags =
            ImGuiDockNodeFlags_NoUndocking |
            ImGuiDockNodeFlags_NoDockingSplit |
            ImGuiDockNodeFlags_NoDockingOverMe |
            ImGuiDockNodeFlags_NoDockingOverOther;
        ImGui::DockSpace(rootDockId, ImVec2(0.0f, 0.0f), lockedTabsOnlyFlags);

        if (ImGui::DockBuilderGetNode(rootDockId) == nullptr) {
            ImGui::DockBuilderRemoveNode(rootDockId);
            ImGui::DockBuilderAddNode(rootDockId, ImGuiDockNodeFlags_DockSpace | lockedTabsOnlyFlags);
            ImGui::DockBuilderSetNodeSize(rootDockId, ImGui::GetContentRegionAvail());
            ImGui::DockBuilderDockWindow("Home##MainDockedTab", rootDockId);
            ImGui::DockBuilderDockWindow("Custom##CustomDockedTab", rootDockId);
            ImGui::DockBuilderDockWindow("Debug##DebugDockedTab", rootDockId);
            ImGui::DockBuilderFinish(rootDockId);
        }
    } ImGui::End();

    ImGuiWindowFlags fixedTabFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBackground;

    if (rootDockId != 0) ImGui::SetNextWindowDockID(rootDockId, ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Debug##DebugDockedTab", nullptr, fixedTabFlags)) debug();
    ImGui::End();

    if (rootDockId != 0) ImGui::SetNextWindowDockID(rootDockId, ImGuiCond_FirstUseEver);
    bool customVisible = ImGui::Begin("Custom##CustomDockedTab", nullptr, fixedTabFlags);
    ImGuiID customDockspaceId = ImGui::GetID("CustomDockspace");
    const ImGuiDockNodeFlags customTabsOnlyFlags = ImGuiDockNodeFlags_AutoHideTabBar;
    const ImGuiDockNodeFlags customDockFlags = customVisible
        ? customTabsOnlyFlags
        : (customTabsOnlyFlags | ImGuiDockNodeFlags_KeepAliveOnly);
    ImGui::DockSpace(customDockspaceId, ImVec2(0.0f, 0.0f), customDockFlags);
    if (customVisible) emptyCustom();
    ImGui::End();

    if (rootDockId != 0) ImGui::SetNextWindowDockID(rootDockId, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Home##MainDockedTab", nullptr, fixedTabFlags)) home();
    ImGui::End();

    drawGeneratedPlots(parseInterface, customDockspaceId, customVisible);
    drawGeneratorUI();
    signalSearch();
    terminal();
    fpsWindow();
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
        static int networkIdx = 3;
        (void)dbcIdx;
        const ImGuiStyle& style = ImGui::GetStyle();
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float labelWidth = ImGui::CalcTextSize("SocketCAN / PCAN").x;
        const float comboOffsetX = labelWidth + (style.ItemInnerSpacing.x * 0.35f);
        const float columnWidth = std::max(120.0f, availableWidth - comboOffsetX - style.FramePadding.x);
        const auto labeledCombo = [&](const char* label, const char* id, int* currentItem, auto& items) {
            const float startX = ImGui::GetCursorPosX();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorPosX(startX + comboOffsetX);
            ImGui::SetNextItemWidth(columnWidth);
            ImGui::Combo(id, currentItem, items.data(), items.size());
        };

        ImGui::BeginGroup();
        labeledCombo("Network", "##network", &networkIdx, availableNetwork);
        if(networkIdx == 0){ currentNetwork = "Server";}
        if(networkIdx == 1){ currentNetwork = "Serial";}
        if(networkIdx == 2){ currentNetwork = "SocketCAN / PCAN";}
        if(networkIdx == 3){ currentNetwork = "Assetto Corsa";}

        if(currentNetwork == "Serial"){
            static int baudIdx = 5;
            static int portIdx = 0;
            labeledCombo("Baud Rate", "##baud_rate", &baudIdx, baudRates);
            if(baudRate != baudRates[baudIdx]){
                rebuildSerial = true;
                baudRate = baudRates[baudIdx];
            }
            refreshSerialPorts();
            if(ports.empty()){
                ImGui::Text("No serial ports detected");
            } else {
                if(portIdx < 0 || static_cast<size_t>(portIdx) >= ports.size()){ portIdx = 0; }
                labeledCombo("Serial Port", "##serial_port", &portIdx, ports);
                if(portIdx >= 0 && static_cast<size_t>(portIdx) < discoveredSerialPorts.size()){
                    if(serialPort != discoveredSerialPorts[portIdx]){
                        rebuildSerial = true;
                        serialPort = discoveredSerialPorts[portIdx];
                    }
                }
            }
        }
        if(currentNetwork == "SocketCAN / PCAN"){
            static int canBitRateIdx = 6;
            static int canPortIdx = 0;
            labeledCombo("Bit Rate", "##can_bitrate", &canBitRateIdx, canBitRates);
            if(canBitRate != canBitRates[canBitRateIdx]){
                rebuildCan = true;
                canBitRate = canBitRates[canBitRateIdx];
            }
            refreshCanPorts();
            if(canPorts.empty()){
                ImGui::TextUnformatted("No CAN interfaces detected");
            } else {
                if(canPortIdx < 0 || static_cast<size_t>(canPortIdx) >= canPorts.size()){ canPortIdx = 0; }
                labeledCombo("Interface", "##can_port", &canPortIdx, canPorts);
                if(canPortIdx >= 0 && static_cast<size_t>(canPortIdx) < discoveredCanPorts.size()){
                    if(canPort != discoveredCanPorts[canPortIdx]){
                        rebuildCan = true;
                        canPort = discoveredCanPorts[canPortIdx];
                    }
                }
            }
        }
        ImGui::EndGroup();
}

void UI::debug(){
    dbcNodes();
};

void UI::home(){
    const ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
    const ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
    const float contentWidth = contentMax.x - contentMin.x;
    const float contentHeight = contentMax.y - contentMin.y;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float sidebarSpacing = style.ItemSpacing.x;
    const float leftSidebarWidth = std::max(1.0f, contentWidth * 0.25f);
    const float rightSidebarWidth = std::max(1.0f, contentWidth * 0.25f);
    const float sidebarHeight = std::max(1.0f, contentHeight);
    if (dbcSelectionIndex != 3 || isCustomDbcSelection(currentDBC)) {
        dbcSelectionIndex = dbcSelectionIndexFromValue(currentDBC);
    }
    if (isCustomDbcSelection(currentDBC)) {
        const std::string selectedCustomPath = customDbcPathFromSelection(currentDBC);
        if (!selectedCustomPath.empty() && std::strcmp(customDbcPath, selectedCustomPath.c_str()) != 0) {
            copyStringToBuffer(selectedCustomPath, customDbcPath, sizeof(customDbcPath));
        }
    }
    const float labelWidth = ImGui::CalcTextSize("SocketCAN / PCAN").x;
    const float comboOffsetX = labelWidth + (style.ItemInnerSpacing.x * 0.35f);

    ImGui::SetCursorPos(contentMin);
    if (ImGui::BeginChild("##home_left_sidebar", ImVec2(leftSidebarWidth, sidebarHeight), true)) {
        const float leftSidebarContentWidth = ImGui::GetContentRegionAvail().x;
        const float controlWidth = std::max(140.0f, leftSidebarContentWidth - comboOffsetX - style.FramePadding.x);
        const auto labeledCombo = [&](const char* label, const char* id, int* currentItem, auto& items) {
            const float startX = ImGui::GetCursorPosX();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorPosX(startX + comboOffsetX);
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::Combo(id, currentItem, items.data(), items.size());
        };
        const int previousDbcSelectionIndex = dbcSelectionIndex;
        labeledCombo("DBC", "##dbc", &dbcSelectionIndex, availableDBC);
        if (dbcSelectionIndex != previousDbcSelectionIndex) {
            if (dbcSelectionIndex == 0) {
                currentDBC = "assettoCorsa";
                dbcStatusMessage.clear();
                dbcStatusIsError = false;
            }
            if (dbcSelectionIndex == 1) {
                currentDBC = "daybreak";
                dbcStatusMessage.clear();
                dbcStatusIsError = false;
            }
            if (dbcSelectionIndex == 2) {
                currentDBC = "vehicle-with-undisclosed-name";
                dbcStatusMessage.clear();
                dbcStatusIsError = false;
            }
        }

        if (dbcSelectionIndex == 3) {
            ImGui::Spacing();
            const float startX = ImGui::GetCursorPosX();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("DBC file");
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorPosX(startX + comboOffsetX);
            ImGui::SetNextItemWidth(controlWidth);
            const bool submittedPath = ImGui::InputText("##custom_dbc_path", customDbcPath, sizeof(customDbcPath), ImGuiInputTextFlags_EnterReturnsTrue);

            ImGui::SetCursorPosX(startX + comboOffsetX);
            if (ImGui::Button("Browse...")) {
                const std::string filePath = openDbcFilePicker();
                if (!filePath.empty()) {
                    copyStringToBuffer(filePath, customDbcPath, sizeof(customDbcPath));
                    currentDBC = makeCustomDbcSelection(filePath);
                    dbcStatusMessage = "Loaded";
                    dbcStatusIsError = false;
                }
            }

            if (submittedPath) {
                const std::string filePath = trimWhitespace(customDbcPath);
                if (filePath.empty()) {
                    dbcStatusMessage = "Failed to load";
                    dbcStatusIsError = true;
                } else {
                    copyStringToBuffer(filePath, customDbcPath, sizeof(customDbcPath));
                    currentDBC = makeCustomDbcSelection(filePath);
                    dbcStatusMessage = "Loaded";
                    dbcStatusIsError = false;
                }
            }

            if (!dbcStatusMessage.empty()) {
                const ImVec4 statusColor = dbcStatusIsError
                    ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
                    : ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
                ImGui::TextUnformatted(dbcStatusMessage.c_str());
                ImGui::PopStyleColor();
            }
        }

        if (!dbcStatusMessage.empty() && dbcSelectionIndex != 3) {
            const ImVec4 statusColor = dbcStatusIsError
                ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
                : ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
            ImGui::TextWrapped("%s", dbcStatusMessage.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        networkUI();
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, sidebarSpacing);

    const float fillerWidth = std::max(0.0f, contentWidth - leftSidebarWidth - rightSidebarWidth - sidebarSpacing);
    ImGui::Dummy(ImVec2(fillerWidth, 1.0f));
    ImGui::SameLine(0.0f, sidebarSpacing);

    if (ImGui::BeginChild("##home_right_sidebar", ImVec2(rightSidebarWidth, sidebarHeight), true)) {
        const ImVec2 rightSidebarAvail = ImGui::GetContentRegionAvail();
        const ImVec2 modelSize(
            std::max(1.0f, rightSidebarAvail.x),
            std::max(1.0f, rightSidebarAvail.y * 0.35f)
        );
        gltfWidget(daybreakModel, modelSize);
        const char* overlayText = "no GPS found...";
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();
        const ImVec2 textSize = ImGui::CalcTextSize(overlayText);
        const float padding = 8.0f;
        const ImVec2 textPos(
            imageMax.x - textSize.x - padding,
            imageMax.y - textSize.y - padding
        );

        drawList->AddText(ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), IM_COL32(0, 0, 0, 160), overlayText);
        drawList->AddText(textPos, IM_COL32(255, 255, 255, 220), overlayText);

        const ImVec2 videoSize(
            std::max(1.0f, rightSidebarAvail.x),
            std::max(1.0f, rightSidebarAvail.y * 0.35f)
        );
        const float videoY = std::max(ImGui::GetCursorPosY(), rightSidebarAvail.y - videoSize.y);
        ImGui::SetCursorPosY(videoY);
        if (videoSource.texture) {
            ImGui::Image(videoSource.texture, videoSize);
        } else {
            ImGui::Dummy(videoSize);
            const ImVec2 videoMin = ImGui::GetItemRectMin();
            const ImVec2 videoMax = ImGui::GetItemRectMax();
            drawList->AddRectFilled(videoMin, videoMax, IM_COL32(70, 70, 70, 255));
        }

        const char* videoOverlayText = "No stream available...";
        const ImVec2 videoMin = ImGui::GetItemRectMin();
        const ImVec2 videoMax = ImGui::GetItemRectMax();
        const ImVec2 videoTextSize = ImGui::CalcTextSize(videoOverlayText);
        const ImVec2 videoTextPos(
            videoMax.x - videoTextSize.x - padding,
            videoMax.y - videoTextSize.y - padding
        );

        drawList->AddText(ImVec2(videoTextPos.x + 1.0f, videoTextPos.y + 1.0f), IM_COL32(0, 0, 0, 160), videoOverlayText);
        drawList->AddText(videoTextPos, IM_COL32(255, 255, 255, 220), videoOverlayText);
    }
    ImGui::EndChild();
}

void UI::dbcNodes(){
    const ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
    const ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
    const float contentWidth = contentMax.x - contentMin.x;

    static bool nodesInitialized = false;
    if(!nodesInitialized){
        ImNodes::SetNodeGridSpacePos(kControlNodeId, ImVec2(760.0f, 80.0f));
        ImNodes::SetNodeGridSpacePos(kSinNodeId, ImVec2(80.0f, 320.0f));
        ImNodes::SetNodeGridSpacePos(kDataNodeId, ImVec2(420.0f, 320.0f));
        ImNodes::SetNodeGridSpacePos(kPlotNodeId, ImVec2(1220.0f, 320.0f));
        ImNodes::SetNodeGridSpacePos(kProceduralStoreNodeId, ImVec2(80.0f, 620.0f));
        nodesInitialized = true;
    }

    const float nodeEditorHeight = std::max( 220.0f, (contentMax.y - contentMin.y) - ImGui::GetStyle().ItemSpacing.y * 2.0f);
    ImGui::SetCursorPos(contentMin);
    ImGui::BeginChild("HomeImNodesExample", ImVec2(contentWidth, nodeEditorHeight), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImNodes::BeginNodeEditor();

    drawSineNode();
    drawDataNode();
    drawPlotNode();
    proceduralDBCNodes();
    //controlsNode();

    ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);
    ImNodes::EndNodeEditor();

    int startAttr = 0;
    int endAttr = 0;
    if(ImNodes::IsLinkCreated(&startAttr, &endAttr)){
        if(selectedProceduralCanId >= 0 &&
           startAttr == 9 &&
           endAttr == proceduralMessageDataInAttrId(selectedProceduralCanId)){
            proceduralDataLink.connected = true;
            proceduralDataLink.canId = selectedProceduralCanId;
        } else if(selectedProceduralCanId >= 0 &&
                  endAttr == 9 &&
                  startAttr == proceduralMessageDataInAttrId(selectedProceduralCanId)){
            proceduralDataLink.connected = true;
            proceduralDataLink.canId = selectedProceduralCanId;
        } else if(parseInterface != nullptr && endAttr == 11){
            for(const auto& [canId, msg] : parseInterface->canStore.canMessages){
                for(size_t signalIndex = 0; signalIndex < msg.signals.size(); ++signalIndex){
                    if(startAttr == proceduralSignalOutAttrId(canId, static_cast<int>(signalIndex))){
                        plotSignalLink.connected = true;
                        plotSignalLink.canId = canId;
                        plotSignalLink.signalIndex = static_cast<int>(signalIndex);
                        goto plot_link_created;
                    }
                }
            }
        } else if(parseInterface != nullptr && startAttr == 11){
            for(const auto& [canId, msg] : parseInterface->canStore.canMessages){
                for(size_t signalIndex = 0; signalIndex < msg.signals.size(); ++signalIndex){
                    if(endAttr == proceduralSignalOutAttrId(canId, static_cast<int>(signalIndex))){
                        plotSignalLink.connected = true;
                        plotSignalLink.canId = canId;
                        plotSignalLink.signalIndex = static_cast<int>(signalIndex);
                        goto plot_link_created;
                    }
                }
            }
        }
    }
plot_link_created:;

    int destroyedLinkId = 0;
    if(ImNodes::IsLinkDestroyed(&destroyedLinkId)){
        if(proceduralDataLink.connected && destroyedLinkId == proceduralDataLink.linkId){
            proceduralDataLink.connected = false;
            proceduralDataLink.canId = -1;
            if(activeEditableLinkId == destroyedLinkId){
                activeEditableLinkId = -1;
            }
        } else if(plotSignalLink.connected && destroyedLinkId == plotSignalLink.linkId){
            plotSignalLink.connected = false;
            plotSignalLink.canId = -1;
            plotSignalLink.signalIndex = -1;
            if(activeEditableLinkId == destroyedLinkId){
                activeEditableLinkId = -1;
            }
        }
    }

    int hoveredLinkId = 0;
    const bool editableLinkHovered = ImNodes::IsLinkHovered(&hoveredLinkId) &&
        ((proceduralDataLink.connected && hoveredLinkId == proceduralDataLink.linkId) ||
         (plotSignalLink.connected && hoveredLinkId == plotSignalLink.linkId));
    if(editableLinkHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
        if((proceduralDataLink.connected && hoveredLinkId == proceduralDataLink.linkId) ||
           (plotSignalLink.connected && hoveredLinkId == plotSignalLink.linkId)){
            activeEditableLinkId = hoveredLinkId;
        }
    }
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !editableLinkHovered){
        activeEditableLinkId = -1;
    }
    if(editableLinkHovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false)){
        if(proceduralDataLink.connected && hoveredLinkId == proceduralDataLink.linkId){
            proceduralDataLink.connected = false;
            proceduralDataLink.canId = -1;
            activeEditableLinkId = -1;
        } else if(plotSignalLink.connected && hoveredLinkId == plotSignalLink.linkId){
            plotSignalLink.connected = false;
            plotSignalLink.canId = -1;
            plotSignalLink.signalIndex = -1;
            activeEditableLinkId = -1;
        }
    }

    if(proceduralDataLink.connected){
        if(activeEditableLinkId == proceduralDataLink.linkId && ImGui::IsKeyPressed(ImGuiKey_Delete, false)){
            proceduralDataLink.connected = false;
            proceduralDataLink.canId = -1;
            activeEditableLinkId = -1;
        }
    }
    if(plotSignalLink.connected){
        if(activeEditableLinkId == plotSignalLink.linkId && ImGui::IsKeyPressed(ImGuiKey_Delete, false)){
            plotSignalLink.connected = false;
            plotSignalLink.canId = -1;
            plotSignalLink.signalIndex = -1;
            activeEditableLinkId = -1;
        }
    }

    ImGui::EndChild();
};

void UI::drawSineNode(){
    constexpr int sinNodeOutAttrId = 7;

    sinNode.amplitude = parseSignedTextOrDefault(sinNode.amplitudeText, sinNode.amplitude);
    sinNode.frequency = parseDoubleTextOrDefault(sinNode.frequencyText, sinNode.frequency);
    const double deltaSeconds = std::max(0.0, static_cast<double>(deltaTime) / 1000.0);
    sinNode.phase += deltaSeconds * sinNode.frequency * (2.0 * IM_PI);
    sinNode.phase = std::fmod(sinNode.phase, 2.0 * IM_PI);
    if(sinNode.phase < 0.0){
        sinNode.phase += 2.0 * IM_PI;
    }
    sinNode.outValue = static_cast<int64_t>(std::llround(std::sin(sinNode.phase) * static_cast<double>(sinNode.amplitude)));

    char sinOutValueText[64] = {};
    std::snprintf(sinOutValueText, sizeof(sinOutValueText), "outValue: %lld", static_cast<long long>(sinNode.outValue));
    static float sinNodeReservedWidth = 0.0f;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float sinNodeProposedWidth = std::max({
        ImGui::CalcTextSize("Sine").x,
        ImGui::CalcTextSize("Amplitude").x,
        ImGui::CalcTextSize("Frequency").x,
        ImGui::CalcTextSize(sinOutValueText).x,
        40.0f + ImGui::CalcTextSize("outValue").x,
        120.0f
    }) + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
    sinNodeReservedWidth = std::max(sinNodeReservedWidth, sinNodeProposedWidth);

    ImNodes::BeginNode(kSinNodeId);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted("Sine");
    ImNodes::EndNodeTitleBar();
    ImGui::Dummy(ImVec2(sinNodeReservedWidth, 0.0f));
    ImGui::PushItemWidth(120.0f);
    ImGui::TextUnformatted("Amplitude");
    ImGui::InputText("##sinAmplitude", sinNode.amplitudeText, IM_ARRAYSIZE(sinNode.amplitudeText),
                     ImGuiInputTextFlags_CharsDecimal);
    ImGui::TextUnformatted("Frequency");
    ImGui::InputText("##sinFrequency", sinNode.frequencyText, IM_ARRAYSIZE(sinNode.frequencyText),
                     ImGuiInputTextFlags_CharsScientific);
    ImGui::PopItemWidth();
    ImGui::TextUnformatted(sinOutValueText);
    ImNodes::BeginOutputAttribute(sinNodeOutAttrId);
    ImGui::Indent(40.0f);
    ImGui::TextUnformatted("outValue");
    ImNodes::EndOutputAttribute();
    ImNodes::EndNode();
}

void UI::drawDataNode(){
    constexpr int dataNodeOutAttrId = 9;

    for(size_t byteIndex = 0; byteIndex < dataNode.data.size(); ++byteIndex){
        dataNode.data[byteIndex] = parseHexByteOrDefault(dataNode.inputText[byteIndex].data(), dataNode.data[byteIndex]);
    }

    ImNodes::BeginNode(kDataNodeId);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted("Data");
    ImNodes::EndNodeTitleBar();
    float widestByteLabel = 0.0f;
    for(int byteIndex = 7; byteIndex >= 0; --byteIndex){
        char label[16] = {};
        std::snprintf(label, sizeof(label), "[%d]", byteIndex);
        widestByteLabel = std::max(widestByteLabel, ImGui::CalcTextSize(label).x);
    }
    if(ImGui::BeginTable("##dataBytes", 2, ImGuiTableFlags_SizingFixedFit)){
        ImGui::TableSetupColumn("byteLabel", ImGuiTableColumnFlags_WidthFixed, widestByteLabel);
        ImGui::TableSetupColumn("byteValue", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        for(int byteIndex = 7; byteIndex >= 0; --byteIndex){
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            char label[16] = {};
            std::snprintf(label, sizeof(label), "[%d]", byteIndex);
            ImGui::TextUnformatted(label);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(52.0f);
            char inputId[24] = {};
            std::snprintf(inputId, sizeof(inputId), "##dataByte%d", byteIndex);
            ImGui::InputText(inputId, dataNode.inputText[static_cast<size_t>(byteIndex)].data(),
                             dataNode.inputText[static_cast<size_t>(byteIndex)].size(),
                             ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
        }
        ImGui::EndTable();
    }
    ImNodes::BeginOutputAttribute(dataNodeOutAttrId);
    ImGui::Indent(40.0f);
    ImGui::TextUnformatted("data");
    ImNodes::EndOutputAttribute();
    ImNodes::EndNode();
}

void UI::drawPlotNode(){
    constexpr int plotNodeInAttrId = 11;
    static GeneratedPlotWindow plotNodeState = []{
        GeneratedPlotWindow plot;
        plot.id = -2000;
        plot.typeIndex = PlotType_Line;
        plot.followLatest = true;
        plot.open = true;
        plot.needsInitialDock = false;
        plot.initialDockNode = 0;
        plot.forceInitialDock = false;
        plot.undockedInteracting = false;
        plot.requestRedock = false;
        return plot;
    }();

    ImNodes::BeginNode(kPlotNodeId);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted("Plot");
    ImNodes::EndNodeTitleBar();
    ImNodes::BeginInputAttribute(plotNodeInAttrId);
    ImGui::TextUnformatted("signal");
    ImNodes::EndInputAttribute();

    const CanMessage* msg = nullptr;
    const CanSignal* signal = nullptr;
    if(plotSignalLink.connected){
        msg = findMessage(parseInterface, plotSignalLink.canId);
        if(msg != nullptr &&
           plotSignalLink.signalIndex >= 0 &&
           static_cast<size_t>(plotSignalLink.signalIndex) < msg->signals.size()){
            signal = &msg->signals[static_cast<size_t>(plotSignalLink.signalIndex)];
        }
    }

    if(msg == nullptr || signal == nullptr){
        ImGui::TextUnformatted("<no signal connected>");
    } else {
        ImGui::TextUnformatted(signal->name.c_str());
        plotNodeState.sources.resize(1);
        plotNodeState.sources[0].canId = plotSignalLink.canId;
        plotNodeState.sources[0].signalIndex = plotSignalLink.signalIndex;
        plotNodeState.followLatest = true;
        plotNodeState.hasView = false;
        if(ImGui::BeginChild("##plotNodeRegion", ImVec2(320.0f, 180.0f), true, ImGuiWindowFlags_NoScrollbar)){
            renderTimeSeriesPlot(parseInterface, plotNodeState);
        }
        ImGui::EndChild();
    }

    ImNodes::EndNode();
}

void UI::drawCanStoreNode(){
    float reservedNodeWidth = 0.0f;
    float listWidth = 260.0f;
    const ImGuiStyle& style = ImGui::GetStyle();
    if(parseInterface != nullptr){
        const MessageOptionsCache& optionCache = getMessageOptionsCache(parseInterface);
        const std::vector<MessageOption>& options = optionCache.options;
        if(proceduralMessageMatchesSignature != optionCache.signature || proceduralMessageMatchesQuery != proceduralMessageQuery){
            std::vector<SourceMatch> matches;
            buildMessageMatches(options, proceduralMessageQuery, matches);
            proceduralMessageMatchIndices.clear();
            proceduralMessageMatchIndices.reserve(matches.size());
            for(const SourceMatch& match : matches){
                if(match.optionIndex >= 0 && match.optionIndex < static_cast<int>(options.size())){
                    proceduralMessageMatchIndices.push_back(match.optionIndex);
                }
            }
            proceduralMessageMatchesSignature = optionCache.signature;
            proceduralMessageMatchesQuery = proceduralMessageQuery;
        }

        reservedNodeWidth = ImGui::CalcTextSize("Can Store").x;
        char summaryText[64] = {};
        std::snprintf(summaryText, sizeof(summaryText), "Messages: %zu", parseInterface->canStore.canMessages.size());
        reservedNodeWidth = std::max(reservedNodeWidth, ImGui::CalcTextSize(summaryText).x);
        if(parseInterface->canStore.canMessages.empty()){
            reservedNodeWidth = std::max(reservedNodeWidth, ImGui::CalcTextSize("No messages available").x);
        } else {
            reservedNodeWidth = std::max(reservedNodeWidth, 240.0f);
            if(proceduralMessageMatchIndices.empty()){
                reservedNodeWidth = std::max(reservedNodeWidth, ImGui::CalcTextSize("No matching messages").x);
            }
            for(int optionIndex : proceduralMessageMatchIndices){
                const float labelWidth = ImGui::CalcTextSize(options[static_cast<size_t>(optionIndex)].label.c_str()).x;
                const float requiredWidth =
                    labelWidth +
                    style.FramePadding.x * 2.0f +
                    style.WindowPadding.x * 2.0f +
                    style.ItemSpacing.x +
                    style.ScrollbarSize;
                listWidth = std::max(listWidth, requiredWidth);
            }
            reservedNodeWidth = std::max(reservedNodeWidth, listWidth);
        }
        reservedNodeWidth = std::max(reservedNodeWidth, 40.0f + ImGui::CalcTextSize("message").x);
    }

    ImNodes::BeginNode(kProceduralStoreNodeId);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted("Can Store");
    ImNodes::EndNodeTitleBar();
    if(reservedNodeWidth > 0.0f){
        ImGui::Dummy(ImVec2(reservedNodeWidth, 0.0f));
    }
    if(parseInterface == nullptr){
        ImGui::TextUnformatted("<no parse interface>");
    } else {
        const MessageOptionsCache& optionCache = getMessageOptionsCache(parseInterface);
        const std::vector<MessageOption>& options = optionCache.options;
        ImGui::Text("Messages: %zu", parseInterface->canStore.canMessages.size());
        if(parseInterface->canStore.canMessages.empty()){
            selectedProceduralCanId = -1;
            ImGui::TextUnformatted("No messages available");
        } else {
            if(parseInterface->canStore.canMessages.find(selectedProceduralCanId) == parseInterface->canStore.canMessages.end()){
                selectedProceduralCanId = parseInterface->canStore.canMessages.begin()->first;
            }

            ImGui::SetNextItemWidth(listWidth);
            ImGui::InputText("##canStoreSearch", proceduralMessageQuery, IM_ARRAYSIZE(proceduralMessageQuery));
            const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
            const float listHeight = std::min(10.0f, std::max(1.0f, static_cast<float>(proceduralMessageMatchIndices.size()))) * rowHeight;
            if(ImGui::BeginChild("##canStoreMessages", ImVec2(listWidth, listHeight), true)){
                if(proceduralMessageMatchIndices.empty()){
                    ImGui::TextUnformatted("No matching messages");
                } else {
                    for(int optionIndex : proceduralMessageMatchIndices){
                        const MessageOption& option = options[static_cast<size_t>(optionIndex)];
                        if(ImGui::Selectable(option.label.c_str(), selectedProceduralCanId == option.canId)){
                            selectedProceduralCanId = option.canId;
                        }
                    }
                }
            }
            ImGui::EndChild();
        }
    }
    ImNodes::BeginOutputAttribute(kProceduralStoreOutAttrId);
    ImGui::Indent(40.0f);
    ImGui::TextUnformatted("message");
    ImNodes::EndOutputAttribute();
    ImNodes::EndNode();
}

void UI::drawCanMessageNode(const CanMessage& msg){
    const int canId = msg.canId;
    ImNodes::BeginNode(proceduralMessageNodeId(canId));
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted("Message");
    ImNodes::EndNodeTitleBar();
    ImNodes::BeginInputAttribute(proceduralMessageInAttrId(canId));
    ImGui::TextUnformatted("store");
    ImNodes::EndInputAttribute();
    ImNodes::BeginInputAttribute(proceduralMessageDataInAttrId(canId));
    ImGui::TextUnformatted("data");
    ImNodes::EndInputAttribute();
    ImGui::TextUnformatted(msg.name.c_str());
    ImGui::Text("canId: 0x%03X", canId);
    ImNodes::BeginOutputAttribute(proceduralMessageOutAttrId(canId));
    ImGui::Indent(40.0f);
    ImGui::TextUnformatted("signals");
    ImNodes::EndOutputAttribute();
    ImNodes::EndNode();
}

void UI::drawCanSignalNode(int canId, int signalIndex, const CanSignal& signal, double outValue){
    const char* typeLabel = "Int";
    if(signal.type == vFLOAT){
        typeLabel = "Float";
    } else if(signal.type == vDOUBLE){
        typeLabel = "Double";
    }

    ImNodes::BeginNode(proceduralSignalNodeId(canId, signalIndex));
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted("Signal");
    ImNodes::EndNodeTitleBar();
    ImNodes::BeginInputAttribute(proceduralSignalInAttrId(canId, signalIndex));
    ImGui::TextUnformatted("message");
    ImNodes::EndInputAttribute();
    ImGui::TextUnformatted(signal.name.c_str());
    ImGui::Text("[%d : %d]", signal.startBit, signal.startBit + signal.length);
    ImGui::TextUnformatted(signal.endianness == 0 ? "Big Endian" : "Little Endian");
    ImGui::TextUnformatted(signal.isSigned ? "Signed" : "Not Signed");
    ImGui::TextUnformatted(typeLabel);
    ImGui::Text("clamp(value * %.6g + %.6g, %.6g, %.6g)", signal.scale, signal.offset, signal.min, signal.max);
    ImGui::Text("outValue: %.6g", outValue);
    ImNodes::BeginOutputAttribute(proceduralSignalOutAttrId(canId, signalIndex));
    ImGui::Indent(40.0f);
    ImGui::TextUnformatted("signal");
    ImNodes::EndOutputAttribute();
    ImNodes::EndNode();
}

void UI::proceduralDBCNodes(){
    drawCanStoreNode();
    if(parseInterface == nullptr){
        return;
    }
    auto selectedIt = parseInterface->canStore.canMessages.find(selectedProceduralCanId);
    if(selectedIt == parseInterface->canStore.canMessages.end()){
        return;
    }

    static std::unordered_set<int> positionedNodes;
    const int canId = selectedIt->first;
    const CanMessage& msg = selectedIt->second;
    const int messageNodeId = proceduralMessageNodeId(canId);
    const bool selectionChanged = (lastProceduralCanId != canId);
    if(selectionChanged && proceduralDataLink.connected){
        if(activeEditableLinkId == proceduralDataLink.linkId){
            activeEditableLinkId = -1;
        }
        proceduralDataLink.connected = false;
        proceduralDataLink.canId = -1;
    }
    if(selectionChanged && plotSignalLink.connected && plotSignalLink.canId != canId){
        if(activeEditableLinkId == plotSignalLink.linkId){
            activeEditableLinkId = -1;
        }
        plotSignalLink.connected = false;
        plotSignalLink.canId = -1;
        plotSignalLink.signalIndex = -1;
    }
    const ImVec2 storePos = ImNodes::GetNodeGridSpacePos(kProceduralStoreNodeId);
    const ImVec2 messagePos = ImVec2(storePos.x + 440.0f, storePos.y + 110.0f);
    if(positionedNodes.insert(messageNodeId).second || selectionChanged){
        ImNodes::SetNodeGridSpacePos(messageNodeId, messagePos);
    }

    drawCanMessageNode(msg);
    ImNodes::Link(proceduralStoreLinkId(canId), kProceduralStoreOutAttrId, proceduralMessageInAttrId(canId));
    const bool useLinkedData = proceduralDataLink.connected && proceduralDataLink.canId == canId;
    std::vector<double> decodedSignalValues;
    if(useLinkedData){
        ImNodes::Link(proceduralDataLink.linkId, 9, proceduralMessageDataInAttrId(canId));
        decodedSignalValues = msg.decodeSignalValues(dataNode.data);
    }

    for(size_t signalIndex = 0; signalIndex < msg.signals.size(); ++signalIndex){
        const int signalNodeId = proceduralSignalNodeId(canId, static_cast<int>(signalIndex));
        const ImVec2 signalPos = ImVec2(messagePos.x + 420.0f, messagePos.y + 30.0f + static_cast<float>(signalIndex) * 110.0f);
        if(positionedNodes.insert(signalNodeId).second || selectionChanged){
            ImNodes::SetNodeGridSpacePos(signalNodeId, signalPos);
        }

        double outValue = 0.0;
        if(useLinkedData){
            outValue = (signalIndex < decodedSignalValues.size()) ? decodedSignalValues[signalIndex] : 0.0;
        } else if(!msg.signals[signalIndex].data.empty()){
            outValue = msg.signals[signalIndex].data.back();
        }
        drawCanSignalNode(canId, static_cast<int>(signalIndex), msg.signals[signalIndex], outValue);
        ImNodes::Link(proceduralSignalLinkId(canId, static_cast<int>(signalIndex)),
                      proceduralMessageOutAttrId(canId),
                      proceduralSignalInAttrId(canId, static_cast<int>(signalIndex)));
    }
    if(plotSignalLink.connected &&
       plotSignalLink.canId == canId &&
       plotSignalLink.signalIndex >= 0 &&
       static_cast<size_t>(plotSignalLink.signalIndex) < msg.signals.size()){
        ImNodes::Link(plotSignalLink.linkId,
                      proceduralSignalOutAttrId(canId, plotSignalLink.signalIndex),
                      11);
    }
    lastProceduralCanId = canId;
}

void UI::makeNodes(){

};

void UI::controlsNode(){
    ImNodes::BeginNode(5);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted("Node Controls");
    ImNodes::EndNodeTitleBar();
    ImGui::TextUnformatted("Connect nodes by dragging Output Pins to Input Pins");
    ImGui::TextUnformatted("Remove a Link by clicking it and pressing Delete");
    ImGui::TextUnformatted("Pan the Canvas with Middle Mouse Button");
    ImNodes::EndNode();
};

void UI::refreshSerialPorts(){
    static auto lastRefresh = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if(lastRefresh != std::chrono::steady_clock::time_point{} &&
       (now - lastRefresh) < std::chrono::milliseconds(1000) &&
       !ports.empty()){
        return;
    }
    lastRefresh = now;

    std::vector<std::string> nextPorts;

#ifdef _WIN32
    nextPorts = enumerateWindowsSerialPorts();
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

void UI::refreshCanPorts(){
    static auto lastRefresh = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if(lastRefresh != std::chrono::steady_clock::time_point{} &&
       (now - lastRefresh) < std::chrono::milliseconds(1000) &&
       !canPorts.empty()){
        return;
    }
    lastRefresh = now;

    std::vector<std::string> nextPorts;

#ifdef _WIN32
    if(discoveredCanPorts.empty()){
        nextPorts.push_back(canPort.empty() ? "PCAN_USBBUS1" : canPort);
    } else {
        nextPorts = discoveredCanPorts;
    }
#else
    namespace fs = std::filesystem;
    std::error_code ec;
    for(const auto& entry : fs::directory_iterator("/sys/class/net", ec)){
        if(ec){ break; }
        std::ifstream typeFile(entry.path() / "type");
        int type = 0;
        if(!(typeFile >> type)){ continue; }
        if(type == 280){
            nextPorts.push_back(entry.path().filename().string());
        }
    }
    std::sort(nextPorts.begin(), nextPorts.end());
#endif

    if(nextPorts.empty()){
        nextPorts.push_back(
#ifdef _WIN32
            canPort.empty() ? "PCAN_USBBUS1" : canPort
#else
            canPort.empty() ? "can0" : canPort
#endif
        );
    }

    if(nextPorts == discoveredCanPorts && !canPorts.empty()){ return; }

    discoveredCanPorts = std::move(nextPorts);
    canPorts.clear();
    canPorts.reserve(discoveredCanPorts.size());
    for(const std::string& port : discoveredCanPorts){
        canPorts.push_back(port.c_str());
    }

    auto current = std::find(discoveredCanPorts.begin(), discoveredCanPorts.end(), canPort);
    if(current == discoveredCanPorts.end()){
        canPort = discoveredCanPorts.front();
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
    if(ImGui::IsKeyReleased(ImGuiKey_F3)) showFps = !showFps;
    if(!showFps) return;
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
    const float totalWidth = popupWindowSize.x + gap + signalSearchPlotSize.x;
    ImVec2 popupWindowPos(center.x - totalWidth * 0.5f, center.y - popupWindowSize.y * 0.5f);
    popupWindowPos = clampPosToViewport(vp, popupWindowPos, popupWindowSize);
    ImGui::SetNextWindowPos(popupWindowPos, ImGuiCond_Always);
    const CanMessage& msg = parseInterface->canStore.canMessages[activeCmdResult.canID];
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
                        signalSearchPlotPos = clampPosToViewport(vp, signalSearchPlotPos, signalSearchPlotSize);
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
    results.reserve(parseInterface->canStore.canMessages.size());

    for (auto& [id, msg] : parseInterface->canStore.canMessages) {
        int d = distance(cmdBuffer, msg.name);
        results.emplace_back(msg.name, d, msg.canId);
    }

    if (results.empty()) {
        cmdSelected = -1;
        return;
    }

    std::sort(results.begin(), results.end(), lessCmdResult);

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
        const VkExtent2D nextExtent = quantizeContentExtent(ImGui::GetContentRegionAvail(), shader.extent);
        if (nextExtent.width != shader.extent.width || nextExtent.height != shader.extent.height) {
            shader.extent = nextExtent;
            shader.dirty = true;
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
        const VkExtent2D nextExtent = quantizeContentExtent(ImGui::GetContentRegionAvail(), obj.extent);
        if (nextExtent.width != obj.extent.width || nextExtent.height != obj.extent.height) {
            obj.extent = nextExtent;
            obj.dirty = true;
        }
        ImVec2 drawSize(obj.extent.width, obj.extent.height);
        drawSize.x = std::max(drawSize.x, 1.0f);
        drawSize.y = std::max(drawSize.y, 1.0f);
        ImGui::Image(obj.outTexture, drawSize);
    } ImGui::End();
}

void UI::gltfWindow(GltfModel& model, std::string windowName){
    ImGui::SetNextWindowSize(ImVec2(model.extent.width, model.extent.height), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration;
    if(ImGui::Begin(windowName.data(), NULL, flags)){
        gltfWidget(model, ImGui::GetContentRegionAvail());
    } ImGui::End();
}

void UI::gltfWidget(GltfModel& model, ImVec2 size) {
    model.deltaSeconds = std::max(0.0f, deltaTime / 1000.0f);
    const VkExtent2D nextExtent = quantizeContentExtent(size, model.extent);
    if (nextExtent.width != model.extent.width || nextExtent.height != model.extent.height) {
        model.extent = nextExtent;
        model.dirty = true;
    }

    ImVec2 drawSize(static_cast<float>(model.extent.width), static_cast<float>(model.extent.height));
    drawSize.x = std::max(drawSize.x, 1.0f);
    drawSize.y = std::max(drawSize.y, 1.0f);
    ImGui::Image(model.texture, drawSize);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        model.wasClicked = true;
    }
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
    style.TabRounding = 0.0f;
    style.WindowRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.FrameRounding = 0.0f;
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

    ImNodesStyle &nodeStyle = ImNodes::GetStyle();
    const auto packNodeColor = [](const ImVec4 &color) {
        return ImGui::ColorConvertFloat4ToU32(color);
    };

    nodeStyle.Colors[ImNodesCol_NodeBackground] = packNodeColor(midGray);
    nodeStyle.Colors[ImNodesCol_NodeBackgroundHovered] = packNodeColor(lightGray);
    nodeStyle.Colors[ImNodesCol_NodeBackgroundSelected] = packNodeColor(lightGray);
    nodeStyle.Colors[ImNodesCol_NodeOutline] = packNodeColor(ImVec4{0.32f, 0.32f, 0.32f, 1.00f});
    nodeStyle.Colors[ImNodesCol_TitleBar] = packNodeColor(darkGray);
    nodeStyle.Colors[ImNodesCol_TitleBarHovered] = packNodeColor(midGray);
    nodeStyle.Colors[ImNodesCol_TitleBarSelected] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_Link] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_LinkHovered] = packNodeColor(ImVec4{0.20f, 0.90f, 0.90f, 1.00f});
    nodeStyle.Colors[ImNodesCol_LinkSelected] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_Pin] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_PinHovered] = packNodeColor(ImVec4{0.20f, 0.90f, 0.90f, 1.00f});
    nodeStyle.Colors[ImNodesCol_BoxSelector] = packNodeColor(ImVec4{0.00f, 0.75f, 0.75f, 0.20f});
    nodeStyle.Colors[ImNodesCol_BoxSelectorOutline] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_GridBackground] = packNodeColor(almostBlack);
    nodeStyle.Colors[ImNodesCol_GridLine] = packNodeColor(ImVec4{0.18f, 0.18f, 0.18f, 1.00f});
    nodeStyle.Colors[ImNodesCol_MiniMapBackground] = packNodeColor(darkGray);
    nodeStyle.Colors[ImNodesCol_MiniMapBackgroundHovered] = packNodeColor(midGray);
    nodeStyle.Colors[ImNodesCol_MiniMapOutline] = packNodeColor(ImVec4{0.32f, 0.32f, 0.32f, 1.00f});
    nodeStyle.Colors[ImNodesCol_MiniMapOutlineHovered] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_MiniMapNodeBackground] = packNodeColor(midGray);
    nodeStyle.Colors[ImNodesCol_MiniMapNodeBackgroundHovered] = packNodeColor(lightGray);
    nodeStyle.Colors[ImNodesCol_MiniMapNodeBackgroundSelected] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_MiniMapNodeOutline] = packNodeColor(ImVec4{0.32f, 0.32f, 0.32f, 1.00f});
    nodeStyle.Colors[ImNodesCol_MiniMapLink] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_MiniMapLinkSelected] = packNodeColor(ImVec4{0.20f, 0.90f, 0.90f, 1.00f});
}
