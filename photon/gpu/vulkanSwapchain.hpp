#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "../engine/include.hpp"
#include "vulkan_core.h"

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
    VkSwapchainKHR swapChain{VK_NULL_HANDLE};
    uint32_t imageCount = 0;
    std::vector<VkImage> images = {};
    std::vector<SwapChainBuffer> swapChainBuffers = {};

#ifdef XCB
    void initSurface(VkInstance instance, xcb_connection_t* connection, xcb_window_t window, VkPhysicalDevice physicalDevice);
#endif
#ifdef WIN
    void initSurface(VkInstance instance, void* platformHandle, void* platformWindow, VkPhysicalDevice physicalDevice);
#endif
    VkResult createSwapChain(uint32_t *width, uint32_t *height, bool vsync, bool fullscreen, bool transparent, VkPhysicalDevice physicalDevice, VkDevice device);
    VkResult acquireNextImage(VkDevice device, VkSemaphore presentCompleteSemaphore, uint32_t* imageIndex);
    VkResult queuePresent(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore);
    void cleanup(VkInstance instance, VkDevice device);
};
