#pragma once
#include "../network/network.hpp"
#include "../parse/parse.hpp"
#include "../gpu/vulkanShader.hpp"
#include "../gpu/vulkanOBJ.hpp"
#include "../gpu/gltf.hpp"
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

struct UI{
    Parse *parseInterface;
    int fontSize = 16;
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
    float deltaTime = 0.0;
    bool showFps = false;
    bool cmdOpen = false;
    bool cmdFF = false;
    char cmdBuffer[128] = {0};
    struct CmdResult { std::string name; int distance = 0; int canID;};
    std::vector<CmdResult> cmdResults;
    CmdResult activeCmdResult{};
    int cmdSelected = -1;
    bool cmdShowPopup = false;
    bool showImGuiTerminalDemo = false;

    VulkanShader accretionShader;
    VulkanShader backgroundShader;
    VulkanShader triangle;

    VulkanObj viking;
    GltfModel daybreakModel;
    
    Video videoSource;

    std::vector<double> globalTime {0.0};

    Console console;

    int styleConfig = 0;

    std::string currentDBC = "assettoCorsa";
    std::vector<const char*> availableDBC = {
            "assettoCorsa",
            "daybreak",
            "vehicle-with-undisclosed-name",
            "Custom file...",
    };
    int dbcSelectionIndex = 0;
    char customDbcPath[1024] = {0};
    std::string customDbcLoadedPath;
    std::string dbcStatusMessage;
    bool dbcStatusIsError = false;

    std::string currentNetwork = "Assetto Corsa";
    std::vector<const char*> availableNetwork ={
        "Server",
        "Serial",
        "SocketCAN / PCAN",
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
    std::string canPort =
#ifdef _WIN32
        "PCAN_USBBUS1";
#else
        "can0";
#endif
    std::string canBitRate = "500000";
    std::vector<const char*> canBitRates = {
        "10000",
        "20000",
        "50000",
        "100000",
        "125000",
        "250000",
        "500000",
        "1000000",
    };
    std::vector<std::string> discoveredCanPorts = {
#ifdef _WIN32
        "PCAN_USBBUS1",
#else
        "can0",
#endif
    };
    std::vector<const char*> canPorts = {
#ifdef _WIN32
        "PCAN_USBBUS1",
#else
        "can0",
#endif
    };
    bool rebuildCan = false;

    struct {
        std::array<uint8_t, 8> data = {};
        std::array<std::array<char, 3>, 8> inputText = {{
            {{'0', '\0', '\0'}},
            {{'0', '\0', '\0'}},
            {{'0', '\0', '\0'}},
            {{'0', '\0', '\0'}},
            {{'0', '\0', '\0'}},
            {{'0', '\0', '\0'}},
            {{'0', '\0', '\0'}},
            {{'0', '\0', '\0'}},
        }};
    } dataNode;

    struct {
        char amplitudeText[32] = "255";
        char frequencyText[32] = "1";
        int64_t outValue = 0;
        int64_t amplitude = 255;
        double frequency = 1.0;
        double phase = 0.0;
    } sinNode;
    struct {
        bool connected = false;
        int canId = -1;
        int linkId = 4000000;
    } proceduralDataLink;
    struct {
        bool connected = false;
        int canId = -1;
        int signalIndex = -1;
        int linkId = 4000001;
    } plotSignalLink;
    int activeEditableLinkId = -1;
    int selectedProceduralCanId = -1;
    int lastProceduralCanId = -1;
    char proceduralMessageQuery[128] = {0};
    std::vector<int> proceduralMessageMatchIndices;
    std::string proceduralMessageMatchesQuery;
    size_t proceduralMessageMatchesSignature = 0;

    void setStyle();
    void setScale();
    void build();
    void fpsWindow();
    void signalSearch();
    void drawGeneratorUI();
    void terminal();
    void basePlate();
    void background();
    void videoWindow();
    void procedural(std::vector<Plot*> plots);
    void defaultWindow(std::string name);
    void orderedWindows(void(*functionArray[])(std::vector<std::vector<double>>&, int, const char*, const char*), size_t count);
    void shaderWindow(VulkanShader& shader, std::string windowName);
    void objWindow(VulkanObj& obj, std::string windowName);
    void gltfWindow(GltfModel& model, std::string windowName);
    void gltfWidget(GltfModel& model, ImVec2 size);
    void GenericPlot(const std::vector<double>& yAxis, const std::vector<double>& xAxis, std::string name);
    void GenericPlotTab(const std::vector<double>& yAxis, const std::vector<double>& xAxis, const char* name);
    void genericInlinePlot(const std::vector<double>& xAxis, const std::vector<double>& yAxis, const char* name);
    void search();
    void installPersistentSettings();
    bool signalSearchPopup();
    void emptyCustom();
    void bottomBar();
    void home();
    void debug();
    void networkUI();
    void dbcNodes();
    void drawSineNode();
    void drawDataNode();
    void drawPlotNode();
    void drawCanStoreNode();
    void drawCanMessageNode(const CanMessage& msg);
    void drawCanSignalNode(int canId, int signalIndex, const CanSignal& signal, double outValue);
    void proceduralDBCNodes();
    void makeNodes();
    void controlsNode();
    void refreshSerialPorts();
    void refreshCanPorts();
    bool signalSearchPlot(const CanSignal& signal, const std::vector<double>& time, ImVec2 pos, ImVec2* outSize = nullptr);
};

int& persistedFontSize();
int distance(std::string a, std::string b);
