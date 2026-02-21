/*[π] the photon gpu interface*/
#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <glm/gtc/matrix_transform.hpp>

#include "vulkanDevice.hpp"
#include "vulkanSwapchain.hpp"
#include "vulkanBuffer.hpp"
#include "camera.hpp"

#define VK_CHECK(x) do { VkResult err = x; if (err) { \
    std::cout << "Detected Vulkan error: " << err << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
    abort(); \
} } while(0)

#define DEFAULT_FENCE_TIMEOUT 100000000000

class Gpu{
public:
    VkInstance instance{ VK_NULL_HANDLE };
    VulkanDevice vulkanDevice {VK_NULL_HANDLE} ;
    VulkanSwapchain vulkanSwapchain;

    std::string title = "Photon";
    std::string name = "Photon";
    uint32_t apiVersion = VK_API_VERSION_1_0;
    std::vector<VkLayerProperties> supportedInstanceLayers;
    std::vector<const char*> enabledInstanceLayers;
    std::vector<std::string> supportedInstanceExtensions;
    std::vector<const char*> enabledInstanceExtensions;
    std::vector<VkPhysicalDevice> physicalDevices;

    VkQueueFlags requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    VkFormat depthFormat;
    struct {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
    } depthStencil{};
    VkRenderPass renderPass{ VK_NULL_HANDLE };
    std::vector<VkFramebuffer> frameBuffers;

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;
    VkDescriptorPool descriptorPool { VK_NULL_HANDLE };

    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;

    VkPipelineStageFlags submitPipelineStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo {};
    uint32_t currentBuffer = 0;
    std::vector<VkFence> waitFences;
    struct {
		VkSemaphore presentComplete;
		VkSemaphore renderComplete;
	} semaphores;

    Camera camera;
    float frameTime = 1.0;
    uint64_t frameCounter = 0;
    const double targetFrameTime = 1000.0 / 300.0; // e.g. if you want 60 FPS → ~16.67ms per frame
    float timerSpeed = 0.25f;
    struct UBOVS{
        glm::mat4 projection;
        glm::mat4 modelView;
        glm::vec4 lightPos;
    } uboVS;
    VulkanBuffer uniformBufferVS;

    VkBool32 getSupportedDepthStencilFormat(VkPhysicalDevice physicalDevice, VkFormat* depthStencilFormat);
    VkBool32 getSupportedDepthFormat(VkPhysicalDevice physicalDevice, VkFormat* depthFormat);
    VkResult createInstance();
    VkResult setupVulkanDevice();

    bool initVulkan();
    void getValidationLayerSupport();
    void createSynchronizationPrimitives(VkDevice device, std::vector<VkCommandBuffer> drawCmdBuffers);
    void setupDepthStencil(uint32_t width, uint32_t height);
    void setupRenderPass(VkDevice device, VkSurfaceFormatKHR surfaceFormat);
    void setupFrameBuffer(VkDevice device, std::vector<SwapChainBuffer> swapChainBuffers, uint32_t imageCount, uint32_t width, uint32_t height);
    void prepareUniformBuffers();
    void updateUniformBuffers(bool animateLight, float lightTimer, float lightSpeed);
    void setupDescriptors(VkDevice device);
    void preparePipelines(VkDevice device);
    static void setImageLayout( VkCommandBuffer cmdbuffer, VkImage image, VkImageLayout oldImageLayout, VkImageLayout newImageLayout, 
            VkImageSubresourceRange subresourceRange, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask);

/* end of gpu class */
};
