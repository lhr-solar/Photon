add_library(Application STATIC
  Application.cpp
)

target_include_directories(Application
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include/Application
)

target_link_libraries(Application
  PRIVATE
    GLFW
    Vulkan::Vulkan
    ImGui
)

