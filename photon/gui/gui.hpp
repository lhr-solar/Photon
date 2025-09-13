/*[μ] the photon graphical user interface*/
#pragma once
#include "xcb/xcb.h"
#include <assert.h>
#include <string>
#include <stdlib.h>
#include <glm/glm.hpp>
#include <array>
#include <vulkan/vulkan.h>
#include "../gpu/vulkanBuffer.hpp"
#include "../gpu/vulkanDevice.hpp"

class Gui{
private:
    void initxcbConnection();
    void setupWindow();

public:
    xcb_connection_t *connection;
    xcb_screen_t *screen;
    xcb_window_t window;
    xcb_intern_atom_reply_t *atom_wm_delete_window;

    uint32_t width = 1280;
    uint32_t height = 720;
    bool viewUpdated = false;
    bool resized = false;

    uint32_t destWidth;
    uint32_t destHeight;

    std::string title = "Photon";
    std::string name = "photon";

    struct{
        bool fullscreen = false;
        bool vsync = false;
        bool transparent = false;
    } settings;

    struct {
        bool displayModels = false;
        bool displayLogos = false;
        bool displayBackground = false;
        bool displayCustomModel = false; // NEW
        bool animateLight = false;
        float lightSpeed = 0.25f;
        float lightTimer = 0.0f;
        std::array<float, 50> frameTimes{};
        float frameTimeMin = 9999.0f, frameTimeMax = 0.0f;
        glm::vec3 modelPosition = glm::vec3(0.0f);
        glm::vec3 modelRotation = glm::vec3(0.0f);
        glm::vec3 modelScale3D = glm::vec3(1.0f);
        float modelScale = 1.0f;
        glm::vec4 effectColor = glm::vec4(1.0f);
        int effectType = 0;
    } renderSettings;

    VkImage fontImage = VK_NULL_HANDLE;
    VkDeviceMemory fontMemory = VK_NULL_HANDLE;
    VkImageView fontView = VK_NULL_HANDLE;

    VkSampler sampler;

    VkDescriptorPool guiDescriptorPool;
    VkDescriptorSetLayout guiDescriptorSetLayout;
    VkDescriptorSet guiDescriptorSet;

    VkPipelineCache guiPipelineCache;
    VkPipelineLayout guiPipelineLayout;
    VkPipeline guiPipeline;

    struct PushConstBlock {
        glm::vec2 scale;
        glm::vec2 translate;
        glm::vec2 invScreenSize;
        glm::vec2 whitePixel;
        glm::vec4 gradTop;
        glm::vec4 gradBottom;
        float u_time;
    } pushConstBlock;

    VulkanBuffer vertexBuffer;
    int32_t vertexCount;
    VulkanBuffer indexBuffer;
    int32_t indexCount;

    Gui();
    ~Gui();
    void initWindow();
    std::string getWindowTitle()const;
    void prepareImGui();
    void initResources(VulkanDevice vulkanDevice, VkRenderPass renderPass);
    void buildCommandBuffers(VulkanDevice vulkanDevice, VkRenderPass renderPass, std::vector<VkFramebuffer> frameBuffers, std::vector<VkCommandBuffer> drawCmdBuffers);
    void updateBuffers(VulkanDevice vulkanDevice);
    void drawFrame(VkCommandBuffer commandBuffer);
/* end of gui class */
};
