# external/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

# Asio (Header-Only)
add_library(external_asio INTERFACE)
target_include_directories(external_asio INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/asio/asio/include
)

# GLFW
add_subdirectory(glfw)

# ImGui
add_subdirectory(imgui)

# Vulkan
# Adjust library names and paths according to your platform:
# On Windows, you'll typically have vulkan-1.lib. On Linux, libvulkan.so.
add_library(Vulkan::Vulkan SHARED IMPORTED)
set_target_properties(Vulkan::Vulkan PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/lib/libvulkan.so"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/include"
)

# Aggregate External Target

add_library(project_external INTERFACE)
target_link_libraries(project_external INTERFACE
    external_asio
    glfw
    Vulkan::Vulkan
    imgui::imgui
)
