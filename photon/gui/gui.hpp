/*[μ] the photon graphical user interface*/
#pragma once
#include <assert.h>
#include <string>
#include <stdlib.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <vulkan/vulkan.h>
#include "ui.hpp"
#include "inputs.hpp"
#include "../gpu/vulkanBuffer.hpp"
#include "../gpu/vulkanDevice.hpp"
#include "../engine/include.hpp"
#include "webcam.hpp"

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
    } imguiPushConst;

    struct CustomShaderResources {
        VkExtent2D extent{512, 512};
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool initialized = false;
    } customShader;

    struct BackgroundShaderResources {
        VkExtent2D extent{0, 0};
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool initialized = false;
    } backgroundShader;

    struct VideoFeedResources {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkExtent2D extent{0, 0};
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool initialized = false;
    } videoFeed;

    VulkanBuffer videoStagingBuffer;
    VkDeviceSize videoStagingBufferSize = 0;
    std::vector<uint8_t> videoFrameData;
    WebcamCapture webcam;
    VkDevice deviceHandle = VK_NULL_HANDLE;

    UI ui;
    Inputs inputs;

    VkImage fontImage = VK_NULL_HANDLE;
    VkDeviceMemory fontMemory = VK_NULL_HANDLE;
    VkImageView fontView = VK_NULL_HANDLE;

    VkSampler sampler = VK_NULL_HANDLE;

    VkDescriptorPool guiDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout guiDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet guiDescriptorSet = VK_NULL_HANDLE;

    VkPipelineCache guiPipelineCache = VK_NULL_HANDLE;
    VkPipelineLayout guiPipelineLayout = VK_NULL_HANDLE;
    VkPipeline guiPipeline = VK_NULL_HANDLE;

    VulkanBuffer vertexBuffer;
    int32_t vertexCount = 0;
    VulkanBuffer indexBuffer;
    int32_t indexCount = 0;

    bool quit = false;


    struct alignas(16) PushConstants {
    glm::vec2 resolution;
    float     u_time;
    float     pad;
    };

    PushConstants pc{};

    Gui();
    ~Gui();

    std::string getWindowTitle()const;
    void prepareImGui();

    void initResources(VulkanDevice vulkanDevice, VkRenderPass renderPass);

    void initCustomShaderResources(VulkanDevice vulkanDevice, VkExtent2D extent);
    void destroyCustomShaderResources(bool releaseDescriptorSet = false);
    VkExtent2D getCustomShaderExtent() const;
    void resizeCustomShader(VulkanDevice vulkanDevice, float width, float height);
    VkExtent2D calculateCustomShaderExtent(float width, float height) const;
    void recordCustomShaderPass(VkCommandBuffer commandBuffer);

    void initBackgroundResources(VulkanDevice vulkanDevice, VkExtent2D extent);
    void destroyBackgroundResources(bool releaseDescriptorSet = false);
    VkExtent2D getBackgroundExtent() const;
    void resizeBackground(VulkanDevice vulkanDevice, float width, float height);
    VkExtent2D calculateBackgroundExtent(float width, float height) const;
    void recordBackgroundPass(VkCommandBuffer commandBuffer);

    void initVideoFeedResources(VulkanDevice vulkanDevice);
    void updateVideoFeed(VulkanDevice vulkanDevice);
    void destroyVideoFeedResources(bool releaseDescriptorSet = false);

    void buildCommandBuffers(VulkanDevice vulkanDevice, VkRenderPass renderPass, std::vector<VkFramebuffer> frameBuffers, std::vector<VkCommandBuffer> drawCmdBuffers);
    void updateBuffers(VulkanDevice vulkanDevice);
    void drawFrame(VkCommandBuffer commandBuffer);

#ifdef XCB
    void initWindow();
    void initxcbConnection();
    void setupWindow();
    void handleEvent(const xcb_generic_event_t *event);
#endif
#ifdef WIN
    void initWindow(HINSTANCE hInstance);
    LRESULT handleMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#endif

/* end of gui class */
};
