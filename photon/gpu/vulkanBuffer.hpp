#pragma once
#include "vulkan/vulkan_core.h"

struct VulkanBuffer{
    VkDevice device;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkDeviceSize alignment = 0;
    VkBufferUsageFlags usageFlags;
    VkMemoryPropertyFlags memoryPropertyFlags;
    VkDescriptorBufferInfo descriptor;
    void* mapped = nullptr;
    VkResult map(VkDeviceSize size , VkDeviceSize offset);
    void unmap();
    VkResult flush(VkDeviceSize size, VkDeviceSize offset);
    void setupDescriptor(VkDeviceSize size, VkDeviceSize offset);
    VkResult bind(VkDeviceSize offset);
};
