#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "../engine/include.hpp"

#ifdef XCB
#include <xcb/xcb.h>
#endif

typedef struct _SwapChainBuffers{
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
} SwapChainBuffer;

struct VulkanSwapchain{
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    uint32_t surfaceQueueNodeIndex{UINT32_MAX};
    VkSurfaceFormatKHR surfaceFormat{};
    VkCommandPool surfaceCommandPool{VK_NULL_HANDLE};
    VkSwapchainKHR swapChain{VK_NULL_HANDLE};
    uint32_t imageCount;
    std::vector<VkImage> images{};
    std::vector<SwapChainBuffer> buffers{};
    std::vector<VkCommandBuffer> drawCmdBuffers;

#ifdef XCB
    void initSurface(VkInstance instance, xcb_connection_t* connection, xcb_window_t window, VkPhysicalDevice physicalDevice);
#endif
    void createSurfaceCommandPool(VkDevice device);
    VkResult createSwapChain(uint32_t *width, uint32_t *height, bool vsync, bool fullscreen, bool transparent, VkPhysicalDevice physicalDevice, VkDevice device);
    void createSurfaceCommandBuffers(VkDevice device);
    VkResult acquireNextImage(VkDevice device, VkSemaphore presentCompleteSemaphore, uint32_t* imageIndex);
    VkResult queuePresent(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore);
};
