#pragma once
#if defined(APPLE) || defined(__APPLE__)
#include "gpuMetal.hpp"
#else
#include <SDL3/SDL.h>
#include <SDL3/SDL_surface.h>
#include <vulkan/vulkan.h>
#include <vulkan_core.h>

#include <array>
#include <atomic>
#include <glm/glm.hpp>
#include <vector>

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

struct TitleBar;

extern std::atomic<uint32_t> gpuAsyncDispatches;

struct AsyncDispatchGuard {
  ~AsyncDispatchGuard() { gpuAsyncDispatches.fetch_sub(1, std::memory_order_relaxed); }
};

struct ImGuiTextureBackendData {
  VkImage image{VK_NULL_HANDLE};
  VkDeviceMemory memory{VK_NULL_HANDLE};
  VkImageView view{VK_NULL_HANDLE};
  VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
  VkBuffer uploadBuffer{VK_NULL_HANDLE};
  VkDeviceMemory uploadMemory{VK_NULL_HANDLE};
  void* uploadMapped = nullptr;
  VkDeviceSize uploadSize = 0;
  VkDeviceSize uploadStride = 0;
  uint32_t uploadSlots = 0;
  bool uploaded = false;
};
struct GPU {
  bool validationLayerSupport();
  void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo);
  void createSwapchainResources();
  void destroySwapchainResources();
  void createFrameResources();
  void destroyFrameResources();

  void init();
  void imguiBackend(TitleBar* titleBar);
  void imguiPresentation(const uint32_t imgIdx);
  void startFrame(uint32_t& imgIdx);
  void startCommands();
  void endCommands();
  void submitFrame(const uint32_t imgIdx);
  void resizeWindow();
  void destroy();
  SDL_Window* createWindow();
  void enableCustomTitlebar(TitleBar* titleBarState);
  void updateImguiDisplayMetrics();
  bool wantsTransparentSwapchain() const;
  VkResult allocateMemory(const VkMemoryAllocateInfo& allocateInfo, VkDeviceMemory* memory);
  void freeMemory(VkDeviceMemory& memory);
  VkResult createBuffer(const VkBufferCreateInfo& createInfo, VkBuffer* buffer);
  void destroyBuffer(VkBuffer& buffer);
  VkResult createImage(const VkImageCreateInfo& createInfo, VkImage* image);
  void destroyImage(VkImage& image);
  VkResult createDescriptorPool(const VkDescriptorPoolCreateInfo& createInfo,
                                VkDescriptorPool* pool);
  void destroyDescriptorPool(VkDescriptorPool& pool);
  VkResult createDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo& createInfo,
                                     VkDescriptorSetLayout* layout);
  void destroyDescriptorSetLayout(VkDescriptorSetLayout& layout);
  VkResult allocateDescriptorSets(const VkDescriptorSetAllocateInfo& allocateInfo,
                                  VkDescriptorSet* sets);
  void freeDescriptorSets(VkDescriptorPool pool, uint32_t count, const VkDescriptorSet* sets);
  VkResult createCommandPool(const VkCommandPoolCreateInfo& createInfo, VkCommandPool* pool);
  void destroyCommandPool(VkCommandPool& pool);
  VkResult allocateCommandBuffers(const VkCommandBufferAllocateInfo& allocateInfo,
                                  VkCommandBuffer* buffers);
  void freeCommandBuffers(VkCommandPool pool, uint32_t count, VkCommandBuffer* buffers);
  VkCompositeAlphaFlagBitsKHR pickCompositeAlpha(
      const VkSurfaceCapabilitiesKHR& surfaceCapabilities);
  void queryWindowPixelSize(uint32_t& outWidth, uint32_t& outHeight) const;
  uint32_t getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags propertyFlags);
  void setImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldImageLayout,
                      VkImageLayout newImageLayout, VkImageSubresourceRange subresourceRange,
                      VkPipelineStageFlags sourceStageMask,
                      VkPipelineStageFlags destinationStageMask);
  VkPipelineShaderStageCreateInfo loadShader(const uint32_t* code, size_t size,
                                             VkShaderModule& module, VkShaderStageFlagBits flagBits,
                                             VkDevice device);
  bool queryPhysicalDeviceId();

  uint32_t width = 1280;
  uint32_t height = 720;
  bool resizePending = false;
  TitleBar* titleBar = nullptr;
  VkInstance instance{VK_NULL_HANDLE};
  SDL_Window* window{NULL};
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
  std::vector<VkCommandBuffer> commandBuffers{};
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
  std::vector<VkDeviceMemory> memoryAllocationHandles{};
  std::vector<VkDeviceSize> memoryAllocationSizes{};
  std::vector<uint32_t> memoryAllocationTypeIndices{};
  std::vector<uint32_t> memoryAllocationHeapIndices{};
  std::vector<VkMemoryPropertyFlags> memoryAllocationPropertyFlags{};
  std::vector<VkBuffer> bufferHandles{};
  std::vector<VkDeviceSize> bufferSizes{};
  std::vector<VkBufferUsageFlags> bufferUsageFlags{};
  std::vector<VkImage> imageHandles{};
  std::vector<VkExtent3D> imageExtents{};
  std::vector<VkFormat> imageFormats{};
  std::vector<VkImageUsageFlags> imageUsageFlags{};
  std::vector<VkSampleCountFlagBits> imageSampleCounts{};
  std::vector<VkDescriptorPool> descriptorPoolHandles{};
  std::vector<uint32_t> descriptorPoolMaxSets{};
  std::vector<VkDescriptorPoolCreateFlags> descriptorPoolFlags{};
  std::vector<VkDescriptorSetLayout> descriptorSetLayoutHandles{};
  std::vector<uint32_t> descriptorSetLayoutBindingCounts{};
  std::vector<VkDescriptorSet> descriptorSetHandles{};
  std::vector<VkDescriptorPool> descriptorSetPoolHandles{};
  std::vector<VkDescriptorSetLayout> descriptorSetLayoutRefs{};
  std::vector<VkCommandPool> commandPoolHandles{};
  std::vector<uint32_t> commandPoolQueueFamilyIndices{};
  std::vector<VkCommandPoolCreateFlags> commandPoolFlags{};
  std::vector<VkCommandBuffer> commandBufferHandles{};
  std::vector<VkCommandPool> commandBufferPoolHandles{};
  std::vector<VkCommandBufferLevel> commandBufferLevels{};
  VkDeviceSize allocatedMemoryBytes{};
  std::vector<VkQueueFamilyProperties> deviceQueueFamilyProperties{};
  VkCommandPool commandPool{};
  VkSampleCountFlagBits msaaSamples{VK_SAMPLE_COUNT_1_BIT};
  std::array<uint8_t, VK_LUID_SIZE> physicalDeviceLuid{};
  bool physicalDeviceLuidValid = false;

  // IMGUI resources
  VkShaderModule uiShaderVert{};
  VkShaderModule uiShaderIndex{};
  VkSampler fontSampler = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool{};
  VkDescriptorSetLayout descriptorSetLayout{};
  VkPipelineLayout imguiPipelineLayout{};
  VkPipeline imguiPipeline;
  ImGuiTextureBackendData backend;
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
  std::vector<void*> vertexBufferMapped{};
  std::vector<void*> indexBufferMapped{};
  std::vector<VkDeviceMemory> vertexBufferMemories{};
  std::vector<VkDeviceMemory> indexBufferMemories{};
  std::vector<uint32_t> vertexIsMapped{};
  std::vector<uint32_t> indexIsMapped{};
  std::vector<std::vector<ImGuiTextureBackendData*>> retiredImguiTextures{};
  uint32_t frameIndex = 0;

#ifdef _WIN32
  bool ensureExternalImageSupport(VkExternalMemoryHandleTypeFlagBits handleType,
                                  bool& requiresDedicated);
  bool selectDirectCompositionHandleType(VkExternalMemoryHandleTypeFlagBits& handleType,
                                         bool& requiresDedicated);
  bool createSharedHandleForTexture(ID3D11Texture2D* texture,
                                    VkExternalMemoryHandleTypeFlagBits handleType,
                                    HANDLE& sharedHandle);
  bool recreateDirectCompositionTargets(uint32_t pixelWidth, uint32_t pixelHeight,
                                        uint32_t imageCount);
  bool resizeDirectCompositionPresenter(uint32_t pixelWidth, uint32_t pixelHeight);
  IDXGIAdapter1* pickDxgiAdapter(IDXGIFactory2* factory);
  bool initDirectCompositionPresenter();
  void destroyDirectCompositionPresenter();
  bool createSharedRenderTargets(uint32_t imageCount);
  void destroySharedRenderTargets();
  void presentWithDirectComposition(uint32_t imageIndex);
  void prepareImageForPresentation(uint32_t imgIdx);
  void adjustSubmitSyncObjects(VkSemaphore& waitSemaphore, VkSemaphore& signalSemaphore) const;
  bool presentFramePlatform(uint32_t imgIdx, uint32_t frameSlot);
  bool startFramePlatform(uint32_t& imgIdx);
  void releasePresentationResources();
  void configureTransparentWindow();
  bool tryActivateDirectComposition(uint32_t imageCount);

  bool directCompositionActive = false;
  void* win32WindowHandle = nullptr;
  VkExternalMemoryHandleTypeFlagBits directCompositionHandleType =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_FLAG_BITS_MAX_ENUM;
  bool directCompositionHandleTypeCached = false;
  bool directCompositionRequiresDedicated = false;
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
#endif
