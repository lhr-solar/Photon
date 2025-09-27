#pragma once
#include "../network/network.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include <vulkan.h>
#include <glm/glm.hpp>
#include <array>

struct UI{
    Network *networkINTF;
    char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {0};
    uint32_t vendorID = 0;
    uint32_t deviceID = 0;
    VkPhysicalDeviceType deviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    uint32_t driverVersion = 0;
    uint32_t apiVersion = 0;

    struct {
        bool displayModels = false;
        bool displayLogos = false;
        bool displayBackground = false;
        bool displayCustomModel = false;
        bool animateLight = false;
        float lightSpeed = 0.25f;
        float lightTimer = 0.0f;
        std::array<float, 50> frameTimes{};
        float frameTimeMin = 9999.0f, frameTimeMax = 0.0f;
        glm::vec3 modelPosition = glm::vec3(0.0f);
        glm::vec3 modelRotation = glm::vec3(0.0f);
        glm::vec3 modelScale3D = glm::vec3(1.0f);
        float modelScale = 1.0f;
        glm::vec4 effectColor = glm::vec4(1.0f);
        int effectType = 0;
    } renderSettings;

    struct CustomShaderPanel : ImVec2 {
        CustomShaderPanel() : ImVec2(512.0f, 512.0f) {}
        ImTextureID texture = static_cast<ImTextureID>(0);
        bool dirty = false;
    } customShader;

    struct BackgroundPanel : ImVec2 {
        BackgroundPanel() : ImVec2(0.0f, 0.0f) {}
        ImTextureID texture = static_cast<ImTextureID>(0);
        bool dirty = false;
    } background;

    ImTextureID videoTexture = static_cast<ImTextureID>(0);
    ImVec2 videoTextureSize = ImVec2(0.0f, 0.0f);

    void build();
    void fpsWindow();
    void customShaderWindow();
    void showVideoDisplay();
    void customBackground();
    void networkSamplePlot();
};
