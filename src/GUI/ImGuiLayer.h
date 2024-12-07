// src/GUI/ImGuiLayer.h

#ifndef IMGUI_LAYER_H
#define IMGUI_LAYER_H

#include <imgui.h>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <functional>

class ImGuiLayer {
public:
    ImGuiLayer(GLFWwindow* window, VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t graphicsQueueFamily, uint32_t imageCount);
    ~ImGuiLayer();

    void beginFrame();
    void endFrame(VkCommandBuffer commandBuffer);

    void setRenderCallback(std::function<void()> callback);

private:
    void initImGui(GLFWwindow* window, VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t graphicsQueueFamily, uint32_t imageCount);

    std::function<void()> renderCallback_;
};

#endif // IMGUI_LAYER_H
