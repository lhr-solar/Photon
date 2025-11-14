#pragma once
#include "../network/network.hpp"
#include "../gpu/vulkanShader.hpp"
#include "../gpu/vulkanOBJ.hpp"
#include "video.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include <vulkan.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include "console.hpp"

struct Plot{
    std::vector<std::vector<double>> data;
    int canID;
    std::string windowName;
    std::string plotName;
    double minValue = 0;
    double maxValue = 1;
    Plot(int canID, const char* windowName, const char* plotName);
    void draw(Network* networkSource);
};

struct UI{
    Network *networkINTF;
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

    VulkanShader accretionShader;
    VulkanShader backgroundShader;
    VulkanShader triangle;

    VulkanObj viking;
    
    Video videoSource;

    void setStyle();
    void build();
    void fpsWindow();
    void basePlate();
    void background();
    void showVideoDisplay();
    void procedural(std::vector<Plot*> plots);
    void defaultWindow(std::string name);
    void orderedWindows(void(*functionArray[])(std::vector<std::vector<double>>&, int, const char*, const char*), size_t count);
    void shaderWindow(VulkanShader& shader, std::string windowName);
    void objWindow(VulkanObj& obj, std::string windowName);

};
