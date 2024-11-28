// src/Renderer/VulkanRenderer.h

#ifndef VULKAN_RENDERER_H
#define VULKAN_RENDERER_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

class VulkanRenderer {
public:
  VulkanRenderer();
  ~VulkanRenderer();

  bool Init(GLFWwindow *window);
  void DrawFrame();
  void Cleanup();

private:
  // Vulkan instance and devices
  VkInstance instance;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device;
  VkSurfaceKHR surface;
  VkRenderPass renderPass;

  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;

  VkPipelineLayout pipelineLayout;
  VkPipeline graphicsPipeline;

  std::vector<VkFramebuffer> swapChainFramebuffers;

  std::vector<VkImageView> swapChainImageViews;

  VkCommandPool commandPool;
  std::vector<VkCommandBuffer> commandBuffers;

  const std::vector<const char *> deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
  };

  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;

  size_t currentFrame = 0;
  const int MAX_FRAMES_IN_FLIGHT = 2;

  // Vulkan queues
  VkQueue graphicsQueue;
  VkQueue presentQueue;

  // Other Vulkan components (swap chain, image views, etc.) would go here

  // Window handle
  GLFWwindow *window;

  // Initialization functions
  bool createInstance();
  bool pickPhysicalDevice();
  bool createLogicalDevice();
  bool createSwapChain();
  bool createImageViews();
  bool createRenderPass();
  bool createGraphicsPipeline();
  VkShaderModule createShaderModule(const std::vector<char> &code);
  bool createFramebuffers();
  bool createCommandPool();
  bool createCommandBuffers();
  bool createSyncObjects();

  struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete();
  } typedef QueueFamilyIndices;

  // Helper functions
  std::vector<const char *> getRequiredExtensions();
  bool checkValidationLayerSupport();
  void populateDebugMessengerCreateInfo(
      VkDebugUtilsMessengerCreateInfoEXT &createInfo);
  bool isDeviceSuitable(VkPhysicalDevice device);
  QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
  bool checkDeviceExtensionSupport(VkPhysicalDevice device);
  SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
  VkSurfaceFormatKHR chooseSwapSurfaceFormat(
      const std::vector<VkSurfaceFormatKHR> &availableFormats);
  VkPresentModeKHR chooseSwapPresentMode(
      const std::vector<VkPresentModeKHR> &availablePresentModes);
  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities);
  std::vector<char> readFile(const std::string &filename);
};

#endif // VULKAN_RENDERER_H
