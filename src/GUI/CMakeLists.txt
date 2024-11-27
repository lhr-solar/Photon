add_library(GUI STATIC
  ImGuiLayer.cpp
)

target_include_directories(GUI
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include/GUI
)

target_link_libraries(GUI
  PRIVATE
    ImGui
)
