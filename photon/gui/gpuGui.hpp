#pragma once
#include <string>
#include <vector>
#include "../gpu/gpu.hpp"
struct gpuGUI{
    static void buildUI(GPU& gpu);
    static std::vector<std::string> VkMemoryPropertyFlagsToString(VkMemoryPropertyFlags flags);
    static std::vector<std::string> VkMemoryHeapFlagsToString(VkMemoryHeapFlags flags);
    static std::vector<std::string> VkQueueFlagsToString(VkQueueFlags flags);
    static std::vector<std::string> VkBufferUsageFlagsToString(VkBufferUsageFlags flags);
    static std::vector<std::string> VkImageUsageFlagsToString(VkImageUsageFlags flags);
    static std::vector<std::string> VkDescriptorPoolCreateFlagsToString(VkDescriptorPoolCreateFlags flags);
    static std::vector<std::string> VkCommandPoolCreateFlagsToString(VkCommandPoolCreateFlags flags);
    static const char* VkFormatToString(VkFormat format);
    static const char* VkSampleCountFlagBitsToString(VkSampleCountFlagBits flags);
    static const char* VkCommandBufferLevelToString(VkCommandBufferLevel level);
};
