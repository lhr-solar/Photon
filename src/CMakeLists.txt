# src/CMakeLists.txt

# Create the executable
add_executable(Photon main.cpp)

target_include_directories(Photon
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_subdirectory(Renderer)

#Link GLFW library
target_link_libraries(Photon PRIVATE Renderer)
