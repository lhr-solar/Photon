#pragma once
#include "../network/network.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>
#include "videoDecoder.hpp"
#include "frame.hpp"

struct UI{
    Network *networkINTF;
    char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {0};
    uint32_t vendorID = 0;
    uint32_t deviceID = 0;
    VkPhysicalDeviceType deviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    uint32_t driverVersion = 0;
    uint32_t apiVersion = 0;

    // Vulkan handles needed for video texture operations
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout imguiDescriptorSetLayout = VK_NULL_HANDLE;
    VkFormat videoTextureFormat = VK_FORMAT_UNDEFINED;

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
    videoDecoder videoDecoder;

    // UI methods
    void setStyle();
    void build();
    void fpsWindow();
    void customShaderWindow();
    void showVideoDisplay();
    void customBackground();
    void networkSamplePlot();
    void showVideoWindow();

    // Vulkan helper methods for video texture management
    void createVideoTexture(uint32_t width, uint32_t height,
                           VkImage& outImage, VkDeviceMemory& outMemory,
                           VkImageView& outView, VkSampler& outSampler);
    
    void uploadVideoFrameToTexture(const frame& videoFrame, VkImage image,
                                   uint32_t width, uint32_t height);
    
    void copyBufferToImage(VkBuffer buffer, VkImage image,
                          uint32_t width, uint32_t height);
    
    void transitionImageLayout(VkImage image, VkFormat format,
                              VkImageLayout oldLayout, VkImageLayout newLayout);
    
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    
    VkDescriptorSet createDescriptorSetForTexture(VkSampler sampler, VkImageView imageView);
};