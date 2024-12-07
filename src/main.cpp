// src/main.cpp

#include "Network/NetworkManager.h"
#include "Renderer/VulkanRenderer.h"
#include "Utils/TelemetryData.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <mutex>

TelemetryData telemetryData;
std::mutex telemetryDataMutex;

void parseTelemetryData(const std::vector<uint8_t> &data,
                        TelemetryData &telemetryData, std::mutex &dataMutex) {
  // Implement parsing logic here
  // This is a placeholder example

  std::lock_guard<std::mutex> lock(dataMutex);

  // For example, suppose data is in the format: speed (4 bytes float), battery
  // level (4 bytes float)
  if (data.size() >= 8) {
    float speed = *reinterpret_cast<const float *>(&data[0]);
    float batteryLevel = *reinterpret_cast<const float *>(&data[4]);

    telemetryData.speed = speed;
    telemetryData.batteryLevel = batteryLevel;
  }
}

int main() {
  // Initialize GLFW
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW\n";
    return -1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  // Create a window
  GLFWwindow *window = glfwCreateWindow(800, 600, "Photon", nullptr, nullptr);
  if (!window) {
    std::cerr << "Failed to create GLFW window\n";
    glfwTerminate();
    return -1;
  }

  // Initialize VulkanRenderer
  VulkanRenderer renderer;
  if (!renderer.Init(window)) {
    std::cerr << "Failed to initialize VulkanRenderer\n";
    glfwDestroyWindow(window);
    glfwTerminate();
    return -1;
  }

  NetworkManager networkManager;
  networkManager.setSerialReadCallback([](const std::vector<uint8_t> &data) {
    parseTelemetryData(data, telemetryData, telemetryDataMutex);
  });
  networkManager.setTcpReadCallback([](const std::vector<uint8_t> &data) {
    parseTelemetryData(data, telemetryData, telemetryDataMutex);
  });
  networkManager.start();

  ImGuiLayer imguiLayer(window, renderer.getInstance(), renderer.getDevice(),
                        renderer.getPhysicalDevice(),
                        renderer.getGraphicsQueueFamilyIndex(),
                        renderer.getSwapChainImageCount());

  // Main loop
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    imguiLayer.beginFrame();

    // Lock telemetry data mutex and display data
    {
      std::lock_guard<std::mutex> lock(telemetryDataMutex);

      ImGui::Begin("Telemetry Data");
      ImGui::Text("Speed: %.2f km/h", telemetryData.speed);
      ImGui::Text("Battery Level: %.2f%%", telemetryData.batteryLevel);
      ImGui::End();
    }

    imguiLayer.endFrame(renderer.getCurrentCommandBuffer());

    // Draw frame
    renderer.DrawFrame();
  }

  // Cleanup
  networkManager.stop();
  renderer.Cleanup();
  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
