#pragma once
#include <xcb/xcb.h>
#include <vulkan/vulkan.h>
#include <vector>

typedef struct _SwapChainBuffers{
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
} SwapChainBuffer;

struct VulkanSwapchain{
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    uint32_t surfaceQueueNodeIndex{UINT32_MAX};
    VkSurfaceFormatKHR surfaceFormat{};
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkSwapchainKHR swapChain{VK_NULL_HANDLE};
    uint32_t imageCount;
    std::vector<VkImage> images{};
    std::vector<SwapChainBuffer> buffers{}; // contains image views
    std::vector<VkCommandBuffer> drawCmdBuffers;


    void initSurface(VkInstance instance, xcb_connection_t* connection, xcb_window_t window, VkPhysicalDevice physicalDevice);
    void createCommandPool(VkDevice device);
    VkResult createSwapChain(uint32_t *width, uint32_t *height, bool vsync, bool fullscreen, bool transparent, VkPhysicalDevice physicalDevice, VkDevice device);
    void createCommandBuffers(VkDevice device);
};
