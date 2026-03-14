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
//#include "../gpu/vulkanGLTF.hpp"
#include "../gpu/vulkanShader.hpp"
#include "../engine/include.hpp"
#include "../network/network.hpp"

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
    UI ui;

    uint32_t width = 1280;
    uint32_t height = 720;
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

    VkDevice deviceHandle = VK_NULL_HANDLE;

    VkPipelineLayout imguiPipelineLayout = VK_NULL_HANDLE;
    VkPipeline imguiGraphicsPipeline = VK_NULL_HANDLE;

    VkImage fontImage = VK_NULL_HANDLE;
    VkDeviceMemory fontMemory = VK_NULL_HANDLE;
    VkImageView fontView = VK_NULL_HANDLE;
    VkSampler fontSampler = VK_NULL_HANDLE;

    Inputs inputs;

    std::vector<VulkanBuffer> vertexBuffers;
    std::vector<int32_t> vertexCounts;
    std::vector<VulkanBuffer> indexBuffers;
    std::vector<int32_t> indexCounts;

    bool quit = false;

    struct PushConstBlock {
        glm::vec2 scale;
        glm::vec2 translate;
        glm::vec2 invScreenSize;
        glm::vec2 whitePixel;
        glm::vec4 gradTop;
        glm::vec4 gradBottom;
        float u_time;
    } imguiPushConst;

    struct alignas(16) PushConstants {
        glm::vec2 resolution;
        float     u_time;
        float     pad;
    }pc;
    float deltaTime = 0.0;

    Gui();
    ~Gui();

    std::string getWindowTitle()const;
    void prepareImGui();
    void refreshFontResources(VulkanDevice vulkanDevice, VkDescriptorSet descriptorSet);

    void initResources(VulkanDevice vulkanDevice, VkRenderPass renderPass, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet descriptorSet);
    void buildCommandBuffers(VulkanDevice vulkanDevice, VkRenderPass renderPass, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet descriptorSet,
            std::vector<VkFramebuffer> frameBuffers, std::vector<VkCommandBuffer> drawCmdBuffers, uint32_t& idx);
    void updateBuffers(VulkanDevice vulkanDevice, uint32_t frameIndex);
    void drawFrame(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, uint32_t frameIndex);

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
