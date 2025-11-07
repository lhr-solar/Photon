#pragma once
#include <vulkan.h>
#include "webcam.hpp"
#include "../gpu/vulkanDevice.hpp"
struct Video{
    WebcamCapture webcam;
    unsigned long long texture;
    VkExtent2D textureSize;

    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    VulkanBuffer stagingBuffer;
    VkDeviceSize stagingBufferSize = 0;
    std::vector<uint8_t> frameData;
    VkExtent2D videoSize;

    bool initialized = false;

    void initVideoFeedResources(VulkanDevice vulkanDevice, VkDescriptorPool descriptorPool,  
        VkDescriptorSetLayout descriptorSetLayout, VkSampler sampler);

    void updateVideoFeed(VulkanDevice vulkanDevice);
    void destroyVideoFeedResources(bool releaseDescriptorSet, VkDevice deviceHandle, VkDescriptorPool descriptorPool);
};
