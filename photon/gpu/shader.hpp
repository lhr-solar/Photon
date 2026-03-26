#pragma once
#include <atomic>
#include <vulkan/vulkan.h>
#include "gpu.hpp"
struct alignas(16) PushConstants {
    glm::vec2 resolution;
    float     u_time;
    float     pad;
};

struct shaderFrame{
    VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
    VkExtent2D extent{256, 256};
    VkImage image{};
    VkImageView view{};
    VkImageLayout layout{};
    VkDeviceMemory imageMemory{};
    VkFramebuffer framebuffer{};
    bool initialized{};
    unsigned long long texture{};
};

struct Shader{
    VkDevice device{};
    VkDescriptorSetLayout descriptorSetLayout{};
    VkDescriptorPool descriptorPool{};
    VkSampler sampler{};
    VkRenderPass renderPass{};
    VkPipelineLayout pipelineLayout{};
    VkPipeline pipeline;
    uint32_t* vertShader;
    size_t vertShaderSize;
    uint32_t* fragShader;
    size_t fragShaderSize;
    std::atomic<bool> initialized{};
    std::atomic<bool> partInitialized{};
    bool dirty{};
    VkShaderModule fragShaderModule{};
    VkShaderModule vertShaderModule{};
    std::vector<shaderFrame> frames{};
    PushConstants pc{};
    uint32_t fif{};
    uint32_t* frameIndex{};

    void init(GPU& gpu, uint32_t* vertexShader, size_t vertexShaderSize,
    uint32_t* fragmentShader, size_t fragmentShaderSize);
    void dispatchInit(GPU& gpu, uint32_t* vertexShader, size_t vertexShaderSize,
    uint32_t* fragmentShader, size_t fragmentShaderSize);
    void prepareInit(GPU& gpu, uint32_t* vertexShader, size_t vertexShaderSize,
    uint32_t* fragmentShader, size_t fragmentShaderSize);
    void finishInit(GPU& gpu);
    void render(GPU& gpu, VkCommandBuffer& commandBuffer);
    void rebuild(GPU& gpu);
    void destroy();
};
