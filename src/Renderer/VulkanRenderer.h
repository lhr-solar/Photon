// src/Renderer/VulkanRenderer.h

#ifndef VULKAN_RENDERER_H
#define VULKAN_RENDERER_H

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <optional>
#include <string>

class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    bool Init(GLFWwindow* window);
    void DrawFrame();
    void Cleanup();

private:
    // Vulkan instance and devices
    VkInstance instance;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device;

    // Vulkan queues
    VkQueue graphicsQueue;
    VkQueue presentQueue;

    // Other Vulkan components (swap chain, image views, etc.) would go here

    // Window handle
    GLFWwindow* window;

    // Initialization functions
    bool createInstance();
    bool pickPhysicalDevice();
    bool createLogicalDevice();

    // Helper functions
    std::vector<const char*> getRequiredExtensions();
    bool checkValidationLayerSupport();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
};

#endif // VULKAN_RENDERER_H
