#pragma once
#include "../network/network.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include <vulkan.h>
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include "../parse/video_decoder.hpp"

struct UI{
    Network *networkINTF;
    char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {0};
    uint32_t vendorID = 0;
    uint32_t deviceID = 0;
    VkPhysicalDeviceType deviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    uint32_t driverVersion = 0;
    uint32_t apiVersion = 0;

    std::string h264VideoPath = "H264Videos/F1TestVid.mp4";
    ImTextureID videoTexture = 0;
    ImVec2 videoTextureSize = ImVec2(0, 0);
    float videoFrameTimer = 0.0f;
    
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

    // H.264 Video decoder
    std::unique_ptr<FFmpegVideoDecoder> h264Decoder;
    bool h264VideoInitialized = false;
    float h264PlaybackSpeed = 30.0f;
    float h264FrameTimer = 0.0f;

    // Getters for decoder params
    FFmpegVideoDecoder* getH264Decoder() { return h264Decoder.get(); }
    const FFmpegVideoDecoder* getH264Decoder() const { return h264Decoder.get(); }
    void setH264Decoder(std::unique_ptr<FFmpegVideoDecoder> decoder) { h264Decoder = std::move(decoder); }

    bool isH264VideoInitialized() const { return h264VideoInitialized; }
    void setH264VideoInitialized(bool initialized) { h264VideoInitialized = initialized; }
    
    float getH264PlaybackSpeed() const { return h264PlaybackSpeed; }
    void setH264PlaybackSpeed(float speed) { h264PlaybackSpeed = speed; }
    
    float getH264FrameTimer() const { return h264FrameTimer; }
    void setH264FrameTimer(float timer) { h264FrameTimer = timer; }
    void updateH264FrameTimer(float deltaTime) { h264FrameTimer += deltaTime; }
    void resetH264FrameTimer() { h264FrameTimer = 0.0f; }
    
    // Convenience checker
    bool isVideoReady() const { return h264VideoInitialized; }

    void setStyle();
    void videoWindow();
    void build();
    void fpsWindow();
    void customShaderWindow();
    void showVideoDisplay();
    void customBackground();
    void networkSamplePlot();
};
