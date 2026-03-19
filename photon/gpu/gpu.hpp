#pragma once
#include <array>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_surface.h>
#include <vulkan/vulkan.h>
#include <vulkan_core.h>
#include <glm/glm.hpp>

#include "../engine/include.hpp"
#include "imgui.h"

#ifdef _WIN32
struct ID3D11Device1;
struct ID3D11DeviceContext1;
struct IDXGISwapChain1;
struct IDXGIAdapter1;
struct IDXGIFactory2;
struct ID3D11Texture2D;
struct IDXGIKeyedMutex;
struct IDCompositionDevice;
struct IDCompositionTarget;
struct IDCompositionVisual;
#endif

struct GPU{
    bool validationLayerSupport();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo);
    void createSwapchainResources();
    void destroySwapchainResources();
    void createFrameResources();
    void destroyFrameResources();

    void init();
    void imguiBackend();
    void imguiPresentation(const uint32_t imgIdx);
    void startFrame(uint32_t& imgIdx);
    void submitFrame(const uint32_t imgIdx);
    void resizeWindow();
    void destroy();
    SDL_Window* createWindow();
    void configureTransparentWindow();
    bool wantsTransparentSwapchain() const;
    VkCompositeAlphaFlagBitsKHR pickCompositeAlpha(const VkSurfaceCapabilitiesKHR& surfaceCapabilities);
    void logCompositeAlphaCapabilities(const VkSurfaceCapabilitiesKHR& surfaceCapabilities) const;
    void prepareImageForPresentation(uint32_t imgIdx);
    void adjustSubmitSyncObjects(VkSemaphore& waitSemaphore, VkSemaphore& signalSemaphore) const;
    bool presentFramePlatform(uint32_t imgIdx, uint32_t frameSlot);
    bool startFramePlatform(uint32_t& imgIdx);
    void queryWindowPixelSize(uint32_t& outWidth, uint32_t& outHeight) const;
    void forceInitialTransparentResize();
    void releasePresentationResources();
    void shutdownPresentationBackend();
#ifdef _WIN32
    bool tryActivateDirectComposition(uint32_t imageCount);
#else
    bool tryActivateDirectComposition(uint32_t) { return false; }
#endif
#ifdef _WIN32
    void logDirectCompositionEvent(const char* stage, const char* detail) const;
    bool ensureExternalImageSupport(VkExternalMemoryHandleTypeFlagBits handleType, bool& requiresDedicated);
    bool selectDirectCompositionHandleType(VkExternalMemoryHandleTypeFlagBits& handleType, bool& requiresDedicated);
    bool createSharedHandleForTexture(ID3D11Texture2D* texture, VkExternalMemoryHandleTypeFlagBits handleType, HANDLE& sharedHandle);
    bool queryPhysicalDeviceId();
    bool recreateDirectCompositionTargets(uint32_t pixelWidth, uint32_t pixelHeight, uint32_t imageCount);
    IDXGIAdapter1* pickDxgiAdapter(IDXGIFactory2* factory);
    bool initDirectCompositionPresenter();
    void destroyDirectCompositionPresenter();
    bool createSharedRenderTargets(uint32_t imageCount);
    void destroySharedRenderTargets();
    void presentWithDirectComposition(uint32_t imageIndex);
    void clearDirectCompositionSwapChain();
    void clearDirectCompositionImages();
#endif

    uint32_t getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags propertyFlags);
    void setImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
            VkImageSubresourceRange subresourceRange, VkPipelineStageFlags sourceStageMask, VkPipelineStageFlags destinationStageMask);
    VkPipelineShaderStageCreateInfo loadShader(const uint32_t* code, size_t size, 
            VkShaderModule& module, VkShaderStageFlagBits flagBits, VkDevice device);

    uint32_t width = 1280;
    uint32_t height = 720;
    VkInstance instance{VK_NULL_HANDLE};
    SDL_Window *window{NULL};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    VkPresentModeKHR presentationMode{};
    VkFormat swapchainFormat{};
    VkRenderPass renderpass{};
    VkColorSpaceKHR swapchainColorspace{};
    std::vector<VkImage> swapchainImages{};
    std::vector<VkImageView> swapchainImageViews{};
    std::vector<VkFramebuffer> framebuffer{VK_NULL_HANDLE};
    std::vector<VkAttachmentDescription> attachmentDescriptions{};
    std::vector<VkSubpassDescription> subpassDescriptions{};
    std::vector<VkSubpassDependency> subpassDependencies{};
    std::vector<VkSemaphore> renderCompleteSemaphores{};
    std::vector<VkSemaphore> imageAvailableSemaphores{};
    std::vector<VkFence> fences{};
    std::vector<VkFence> imagesInFlight{};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    float queuePriority = 1.0f;
    uint32_t queueFamilyIndex{};
    uint32_t queueIndex{};
    uint32_t queueCount{};
    VkPhysicalDeviceProperties deviceProperties{};
    VkPhysicalDeviceFeatures deviceFeatures{};
    VkPhysicalDeviceMemoryProperties deviceMemoryProperties{};
    std::vector<VkQueueFamilyProperties> deviceQueueFamilyProperties{};
    VkCommandPool commandPool{};
    std::vector<VkCommandBuffer> commandBuffers{};

    // IMGUI resources
    VkShaderModule uiShaderVert{};
    VkShaderModule uiShaderIndex{};
    VkImage fontImage = VK_NULL_HANDLE;
    VkDeviceMemory fontMemory = VK_NULL_HANDLE;
    VkImageView fontView = VK_NULL_HANDLE;
    VkSampler fontSampler = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool{};
    VkDescriptorSetLayout descriptorSetLayout{};
    VkDescriptorSet descriptorSet{};
    VkPipelineLayout imguiPipelineLayout{};
    VkPipeline imguiPipeline;
    struct PushConstBlock {
        glm::vec2 scale;
        glm::vec2 translate;
    } imguiPushConst;

    std::vector<VkBuffer> vertexBuffers{};
    std::vector<VkBuffer> indexBuffers{};
    std::vector<int32_t> vertexCounts{};
    std::vector<int32_t> indexCounts{};
    std::vector<VkDeviceSize> vertexBufferSizes{};
    std::vector<VkDeviceSize> indexBufferSizes{};
    std::vector<void *> vertexBufferMapped{};
    std::vector<void *> indexBufferMapped{};
    std::vector<VkDeviceMemory> vertexBufferMemories{};
    std::vector<VkDeviceMemory> indexBufferMemories{};
    std::vector<uint32_t> vertexIsMapped{};
    std::vector<uint32_t> indexIsMapped{};

    uint32_t frameIndex = 0;
#ifdef _WIN32
    bool directCompositionActive = false;
    VkExternalMemoryHandleTypeFlagBits directCompositionHandleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
    bool directCompositionRequiresDedicatedMemory = false;
    uint32_t directCompositionWidth = 0;
    uint32_t directCompositionHeight = 0;
    bool transparentResizeHackApplied = false;
    void* win32WindowHandle = nullptr;
    std::array<uint8_t, VK_LUID_SIZE> physicalDeviceLuid{};
    bool physicalDeviceLuidValid = false;
    struct SharedDirectImage {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        HANDLE sharedHandle = NULL;
        ID3D11Texture2D* d3dTexture = nullptr;
        IDXGIKeyedMutex* keyedMutex = nullptr;
    };
    struct D3DPresenter {
        IDXGIFactory2* dxgiFactory = nullptr;
        ID3D11Device1* d3dDevice = nullptr;
        ID3D11DeviceContext1* d3dContext = nullptr;
        IDXGISwapChain1* swapChain = nullptr;
        IDCompositionDevice* dcompDevice = nullptr;
        IDCompositionTarget* dcompTarget = nullptr;
        IDCompositionVisual* dcompVisual = nullptr;
        uint32_t bufferCount = 0;
    } d3dPresenter{};
    std::vector<SharedDirectImage> directImages{};
#endif
};
