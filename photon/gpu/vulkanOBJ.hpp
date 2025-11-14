#pragma once
#include "vulkanDevice.hpp"
#include "vulkanShader.hpp"
#include <array>

struct Vertex{
    glm::vec2 pos;
    glm::vec3 color;
    static VkVertexInputBindingDescription getBindingDescription(){
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    };
    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescription(){
        std::array<VkVertexInputAttributeDescription, 2> attributeDescription{};

        return attributeDescription;
    }
};

struct VulkanObj{
    VkExtent2D extent;
    VkImage image;
    VkDeviceMemory imageMemory;
    VkImageView imageView;
    VkImageLayout imageLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkFramebuffer frameBuffer;
    VkRenderPass renderPass;
    bool dirty = false;
    uint32_t* vertShader; 
    size_t vertShaderSize; 
    uint32_t* fragShader; 
    size_t fragShaderSize;
    struct alignas(16) PushConstants {
        glm::vec2 resolution;
        float     u_time;
        float     pad;
    }pushConstants;
    struct UniformBuffer{
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 projection;
    } uniformBufferObject;
    VkBuffer uniformBuffer;
    VkSampler sampler;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet uniformDescriptorSet = VK_NULL_HANDLE;
    unsigned long long outTexture;
    bool initialized = false;
    std::vector<Vertex> vertices = {
        {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
    };

    void initObj(VkExtent2D extent, uint32_t* vertShader, size_t vertShaderSize, uint32_t* fragShader, size_t fragShaderSize);
    void createResources(VulkanDevice device, VkExtent2D extent, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout);
    void recordRenderPass(VkCommandBuffer commandBuffer);
};

