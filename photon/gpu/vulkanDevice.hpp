#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include "vulkanBuffer.hpp"

struct VulkanDevice {
    VkPhysicalDevice physicalDevice;
    VkDevice logicalDevice;

    VkPhysicalDeviceProperties deviceProperties{};
	VkPhysicalDeviceFeatures deviceFeatures{};
	VkPhysicalDeviceMemoryProperties deviceMemoryProperties{};
    std::vector<VkQueueFamilyProperties> queueFamilyProperties;
    std::vector<std::string> supportedExtensions;

    VkPhysicalDeviceFeatures enabledFeatures{};

    std::vector<const char*> enabledDeviceExtensions;

    VkCommandPool graphicsCommandPool = VK_NULL_HANDLE;
    VkCommandPool computeCommandPool = VK_NULL_HANDLE;
    VkCommandPool transferCommandPool = VK_NULL_HANDLE;

    struct {
		uint32_t graphics;
		uint32_t compute;
		uint32_t transfer;
	} queueFamilyIndices;

    VkQueue graphicsQueue;
    VkQueue computeQueue;
    VkQueue transferQueue;

    VkResult initDevice(VkPhysicalDevice physicalDevice);
    VkResult createLogicalDevice(VkPhysicalDeviceFeatures enabledFeatures, std::vector<const char*> enabledExtensions, void* pNextChain, bool useSwapChain, VkQueueFlags requestedQueueTypes);
    uint32_t getQueueFamilyIndex(VkQueueFlags queueFlags) const;
    bool extensionSupported(std::string extension);
    VkCommandPool createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags);
    uint32_t getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 * memTypeFound) const;
    VkResult createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VulkanBuffer *buffer, VkDeviceSize size, void *data);
};
