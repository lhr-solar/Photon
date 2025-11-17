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
#include "plot.hpp"

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

//    VulkanObj viking;
    
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
//    void objWindow(VulkanObj& obj, std::string windowName);
    void debugWindow();
    void GenericPlot(const std::vector<double>& yAxis, const std::vector<double>& xAxis, std::string name);
    void GenericPlotTab(const std::vector<double>& yAxis, const std::vector<double>& xAxis, const char* name);
    void debugWindowTab();

};
