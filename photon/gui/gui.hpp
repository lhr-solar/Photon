/*[μ] the photon graphical user interface*/
#pragma once
#include <assert.h>
#include <string>
#include <stdlib.h>
#include <glm/glm.hpp>
#include <array>
#include <vulkan/vulkan.h>
#include "ui.hpp"
#include "inputs.hpp"
#include "../gpu/vulkanBuffer.hpp"
#include "../gpu/vulkanDevice.hpp"
#include "../engine/include.hpp"

#ifdef XCB
#include "xcb/xcb.h"
#endif

class Gui{
public:
#ifdef XCB
    xcb_connection_t *connection;
    xcb_screen_t *screen;
    xcb_window_t window;
    xcb_intern_atom_reply_t *atom_wm_delete_window;
#endif
#ifdef WIN
    HINSTANCE windowInstance;
    HWND window;
#endif

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
        bool transparent = true;
    } settings;

    struct PushConstBlock {
        glm::vec2 scale;
        glm::vec2 translate;
        glm::vec2 invScreenSize;
        glm::vec2 whitePixel;
        glm::vec4 gradTop;
        glm::vec4 gradBottom;
        float u_time;
    } pushConstBlock;

    struct CustomShaderResources {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkExtent2D extent{512, 512};
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool initialized = false;
    } customShader;

    UI ui;
    Inputs inputs;

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

    VulkanBuffer vertexBuffer;
    int32_t vertexCount;
    VulkanBuffer indexBuffer;
    int32_t indexCount;

    bool quit = false;

    Gui();
    ~Gui();

#ifdef XCB
    void initWindow();
    void initxcbConnection();
    void setupWindow();
    void handleEvent(const xcb_generic_event_t *event);
#endif
#ifdef WIN
    void initWindow(HINSTANCE hInstance, WNDPROC wndproc);
#endif

    std::string getWindowTitle()const;
    void prepareImGui();
    void initResources(VulkanDevice vulkanDevice, VkRenderPass renderPass);
    void initCustomShaderResources(VulkanDevice vulkanDevice);
    void buildCommandBuffers(VulkanDevice vulkanDevice, VkRenderPass renderPass, std::vector<VkFramebuffer> frameBuffers, std::vector<VkCommandBuffer> drawCmdBuffers);
    void updateBuffers(VulkanDevice vulkanDevice);
    void drawFrame(VkCommandBuffer commandBuffer);
    void recordCustomShaderPass(VkCommandBuffer commandBuffer);
/* end of gui class */
};
