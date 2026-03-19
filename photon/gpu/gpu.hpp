#pragma once
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_surface.h>
#include <vulkan/vulkan.h>
#include <vulkan_core.h>
#include <glm/glm.hpp>

#include "../engine/include.hpp"
#include "imgui.h"

struct GPU{
    bool validationLayerSupport();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo);

    void init();
    void imguiBackend();
    void imguiPresentation(const uint32_t imgIdx);
    void startFrame(uint32_t& imgIdx);
    void submitFrame(const uint32_t imgIdx);
    void resizeWindow();
    void destroy();

    uint32_t getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags propertyFlags);
    void setImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
            VkImageSubresourceRange subresourceRange, VkPipelineStageFlags sourceStageMask, VkPipelineStageFlags destinationStageMask);
    VkPipelineShaderStageCreateInfo loadShader(const uint32_t* code, size_t size, VkShaderModule& module, VkShaderStageFlagBits flagBits, VkDevice device);

    uint32_t width = 640;
    uint32_t height = 480;
    VkInstance instance{VK_NULL_HANDLE};
    SDL_Window *window{NULL};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    VkPresentModeKHR presentationMode{};
    VkFormat swapchainFormat{};
    VkRenderPass renderpass{};
    VkColorSpaceKHR swapchainColorspace{};
    std::vector<VkImage> swapchainImages{};
    std::vector<VkImageView> swapchainImageViews{};
    std::vector<VkFramebuffer> framebuffer{VK_NULL_HANDLE};
    std::vector<VkAttachmentDescription> attachmentDescriptions{};
    std::vector<VkSubpassDescription> subpassDescriptions{};
    std::vector<VkSubpassDependency> subpassDependencies{};
    std::vector<VkSemaphore> renderCompleteSemaphores{};
    std::vector<VkSemaphore> imageAvailableSemaphores{};
    std::vector<VkFence> fences{};
    std::vector<VkFence> imagesInFlight{};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    float queuePriority = 1.0f;
    uint32_t queueFamilyIndex{};
    uint32_t queueIndex{};
    uint32_t queueCount{};
    VkPhysicalDeviceProperties deviceProperties{};
    VkPhysicalDeviceFeatures deviceFeatures{};
    VkPhysicalDeviceMemoryProperties deviceMemoryProperties{};
    std::vector<VkQueueFamilyProperties> deviceQueueFamilyProperties{};
    VkCommandPool commandPool{};
    std::vector<VkCommandBuffer> commandBuffers{};

    // IMGUI resources
    VkShaderModule uiShaderVert{};
    VkShaderModule uiShaderIndex{};
    VkImage fontImage = VK_NULL_HANDLE;
    VkDeviceMemory fontMemory = VK_NULL_HANDLE;
    VkImageView fontView = VK_NULL_HANDLE;
    VkSampler fontSampler = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool{};
    VkDescriptorSetLayout descriptorSetLayout{};
    VkDescriptorSet descriptorSet{};
    VkPipelineLayout imguiPipelineLayout{};
    VkPipeline imguiPipeline;
    struct PushConstBlock {
        glm::vec2 scale;
        glm::vec2 translate;
    } imguiPushConst;

    std::vector<VkBuffer> vertexBuffers{};
    std::vector<VkBuffer> indexBuffers{};
    std::vector<int32_t> vertexCounts{};
    std::vector<int32_t> indexCounts{};
    std::vector<VkDeviceSize> vertexBufferSizes{};
    std::vector<VkDeviceSize> indexBufferSizes{};
    std::vector<void *> vertexBufferMapped{};
    std::vector<void *> indexBufferMapped{};
    std::vector<VkDeviceMemory> vertexBufferMemories{};
    std::vector<VkDeviceMemory> indexBufferMemories{};
    std::vector<uint32_t> vertexIsMapped{};
    std::vector<uint32_t> indexIsMapped{};

    uint32_t frameIndex = 0;
};
