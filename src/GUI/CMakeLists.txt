#src/GUI/CMakeLists.txt

add_library(gui_lib ImGuiLayer.cpp)
target_include_directories(gui_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gui_lib PUBLIC imgui::imgui)
