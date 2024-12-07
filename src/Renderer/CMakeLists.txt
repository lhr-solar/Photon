# Renderer/CMakeLists.txt
add_library(renderer_lib VulkanRenderer.cpp)

target_include_directories(renderer_lib PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(renderer_lib PUBLIC Vulkan::Vulkan glfw)
