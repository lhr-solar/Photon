#pragma once
#include "vulkan_core.h"
#include "vulkanDevice.hpp"
#include <glm/glm.hpp>
struct VulkanShader{
    VkExtent2D extent{0, 0};
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    bool initialized = false;
    struct alignas(16) PushConstants {
        glm::vec2 resolution;
        float     u_time;
        float     pad;
    }pc;
    uint32_t* vertexShader;
    size_t vertexShaderSize;
    uint32_t* fragmentShader;
    size_t fragmentShaderSize;
    bool runtimeFrag = false;
    std::string fragmentShaderName;
    unsigned long long texture;
    bool dirty;

    void initShader(VkExtent2D extent, bool dynamic, 
            uint32_t* vertexShader, size_t vertexShaderSize, 
            uint32_t* fragmentShader, size_t fragmentShaderSize, std::string fragName);

    void createResources(VulkanDevice vulkanDevice, VkExtent2D extent, VkDescriptorPool descriptorPool, 
            VkDescriptorSetLayout descriptorSetLayout, VkPipelineCache pipelineCache, VkSampler sampler);
    void destroyResources(bool releaseDescriptor, VkDevice deviceHandle, 
            VkDescriptorPool descriptorPool);
    void recordShaderPass(VkCommandBuffer commandBuffer);
 };
