# src/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

# Add subdirectories for each component
add_subdirectory(Application)
add_subdirectory(GUI)
add_subdirectory(Network)
add_subdirectory(Renderer)
add_subdirectory(Utils)

# Create the main executable from main.cpp
add_executable(Photon main.cpp)

# Link external dependencies (from external/) and internal libraries
target_link_libraries(Photon PRIVATE
    project_external  # from external/CMakeLists.txt
    application_lib   # from Application/CMakeLists.txt
    gui_lib           # from GUI/CMakeLists.txt
    network_lib       # from Network/CMakeLists.txt
    renderer_lib      # from Renderer/CMakeLists.txt
    utils_lib         # from Utils/CMakeLists.txt
)

# Include directories if needed
target_include_directories(Photon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

