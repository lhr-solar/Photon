# Renderer/CMakeLists.txt
add_library(Renderer STATIC
  VulkanRenderer.cpp
)

target_include_directories(Renderer
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include/Renderer
)

target_link_libraries(Renderer
  PUBLIC
    Vulkan::Vulkan
    glfw
)
