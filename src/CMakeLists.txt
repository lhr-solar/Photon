# Add subdirectories for each module
add_subdirectory(Application)
add_subdirectory(Renderer)
add_subdirectory(GUI)
add_subdirectory(Network)
add_subdirectory(Utils)

# Create the executable
add_executable(Photon main.cpp)

# Link the modules
target_link_libraries(Photon
  PRIVATE
    Application
    Renderer
    GUI
    Network
    Utils
)

# Include directories
target_include_directories(Photon
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
)
