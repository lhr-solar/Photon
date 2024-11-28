// src/main.cpp

#include "Renderer/VulkanRenderer.h"
#include <GLFW/glfw3.h>
#include <iostream>

int main() {
  // Initialize GLFW
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW\n";
    return -1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  // Create a window
  GLFWwindow *window =
      glfwCreateWindow(800, 600, "Photon Dashboard", nullptr, nullptr);
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

  // Main loop
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    // Draw frame
    renderer.DrawFrame();
  }

  // Cleanup
  renderer.Cleanup();
  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
