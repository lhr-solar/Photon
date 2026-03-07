#pragma once
#include "../network/network.hpp"
#include "../parse/parse.hpp"
#include "../gpu/vulkanShader.hpp"
#include "../gpu/vulkanOBJ.hpp"
#include "video.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include <vulkan.h>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <vector>
#include "console.hpp"
#include "plot.hpp"

namespace ax::NodeEditor {
struct EditorContext;
}

struct UI{
    Parse *parseINTF;
    int fontSize = 24;
    int fontSizeMin = 8;
    int fontSizeMax = 96;
    bool fontSizeSynced = false;
    bool fontSizeDirty = false;
    char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {0};
    uint32_t vendorID = 0;
    uint32_t deviceID = 0;
    VkPhysicalDeviceType deviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    uint32_t driverVersion = 0;
    uint32_t apiVersion = 0;

    struct {
        std::array<float, 50> frameTimes{};
        float frameTimeMin = 9999.0f, frameTimeMax = 0.0f;
    } renderSettings;

    bool cmdOpen = false;
    bool cmdFF = false;
    char cmdBuffer[128] = {0};
    struct CmdResult { std::string name; int distance = 0; int canID;};
    std::vector<CmdResult> cmdResults;
    CmdResult activeCmdResult{};
    int cmdSelected = -1;
    bool cmdShowPopup = false;
    bool showImGuiTerminalDemo = false;
    ax::NodeEditor::EditorContext* nodeEditorContext = nullptr;
    bool nodeEditorFirstFrame = true;

    VulkanShader accretionShader;
    VulkanShader backgroundShader;
    VulkanShader triangle;

    VulkanObj viking;
    
    Video videoSource;

    std::vector<double> globalTime {0.0};

    Console console;

    int styleConfig = 0;

    std::string currentDBC = "assettoCorsa";
    std::vector<const char*> availableDBC = {
            "assettoCorsa",
            "daybreak",
    };

    std::string currentNetwork = "Assetto Corsa";
    std::vector<const char*> availableNetwork ={
        "Server",
        "Serial",
        "Assetto Corsa",
    };
    std::string baudRate = "9600";
    std::vector<const char*> baudRates = {
        "600",
        "1200",
        "1800",
        "2400",
        "4800",
        "9600",
        "19200",
        "38400U",
    };
    std::string serialPort =
#ifdef _WIN32
        "COM1";
#else
        "/dev/ttyUSB0";
#endif
    std::vector<std::string> discoveredSerialPorts = {
#ifdef _WIN32
        "COM1",
#else
        "/dev/ttyUSB0",
#endif
    };
    std::vector<const char*> ports = {
#ifdef _WIN32
        "COM1",
#else
        "/dev/ttyUSB0",
#endif
    };
    bool rebuildSerial = false;

    void setStyle();
    void setScale();
    void build();
    void fpsWindow();
    void signalSearch();
    void terminal();
    void nodeEditorDemo();
    void basePlate();
    void background();
    void videoWindow();
    void procedural(std::vector<Plot*> plots);
    void defaultWindow(std::string name);
    void orderedWindows(void(*functionArray[])(std::vector<std::vector<double>>&, int, const char*, const char*), size_t count);
    void shaderWindow(VulkanShader& shader, std::string windowName);
    void objWindow(VulkanObj& obj, std::string windowName);
    void GenericPlot(const std::vector<double>& yAxis, const std::vector<double>& xAxis, std::string name);
    void GenericPlotTab(const std::vector<double>& yAxis, const std::vector<double>& xAxis, const char* name);
    void genericInlinePlot(const std::vector<double>& xAxis, const std::vector<double>& yAxis, const char* name);
    void tempWork();
    void search();
    void installPersistentSettings();
    bool signalSearchPopup();
    void bottomBar();
    void home();
    void networkUI();
    void refreshSerialPorts();
    bool signalSearchPlot(const CanSignal& signal, const std::vector<double>& time, ImVec2 pos, ImVec2* outSize = nullptr);
};

int distance(std::string a, std::string b);
