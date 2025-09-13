cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/find")

project(Photon VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Cross-compilation specific settings
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compiling for ${CMAKE_SYSTEM_PROCESSOR}")
    
    # Skip some checks that are problematic in cross-compilation
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    
    # Set compiler flags for ARM64
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=cortex-a72 -O2")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=cortex-a72 -O2")
endif()

# Include directories
include_directories(.external)
include_directories(.external/glm)
include_directories(.external/vulkan)
include_directories(.external/vk_video)
include_directories(.external/stb)
include_directories(.external/imgui)
include_directories(/usr/include/vulkan)

# Dependency handling
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compilation mode for ${CMAKE_SYSTEM_PROCESSOR}")
    
    # For cross-compilation, set libraries manually
    set(Vulkan_FOUND ON)
    set(Vulkan_LIBRARY vulkan)
    set(XCB_FOUND ON) 
    set(XCB_LIBRARIES xcb xcb-keysyms)
    set(Python3_FOUND ON)
    
    # Additional libraries needed for ARM64/Raspberry Pi
    set(ADDITIONAL_LIBS pthread dl m)
    
    message(STATUS "Cross-compilation libraries configured")
else()
    # Normal native compilation dependency finding
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_library(Vulkan_LIBRARY NAMES vulkan REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB REQUIRED xcb xcb-keysyms)
    set(ADDITIONAL_LIBS)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_XCB_KHR")

# Link libraries
link_libraries(${XCB_LIBRARIES} ${Vulkan_LIBRARY} ${ADDITIONAL_LIBS})

# Build kernels (shaders) if Python is available
# For cross-compilation, we can still compile shaders on the host
if(Python3_FOUND)
    add_subdirectory(kernels)
endif()

add_subdirectory(photon)
