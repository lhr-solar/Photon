# Photon/CMakeLists.txt

cmake_minimum_required(VERSION 3.30)
project(Photon)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Vulkan Debug Layers
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}" -DDEBUG)

#Find Vulkan and Boost
#find_package(Vulkan REQUIRED)
#find_package(Boost 1.71.0 REQUIRED COMPONENTS system)

# Add subdirectories
add_subdirectory(external)
add_subdirectory(src)
#add_subdirectory(tests)
