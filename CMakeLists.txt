#/CMakeLists.txt
cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/find")

set(NAME Photon)
project(NAME VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wno-psabi)

include_directories(.external)
include_directories(.external/glm)
include_directories(.external/vulkan)
include_directories(.external/vk_video)

# find Vulkan
IF(NOT Vulkan_FOUND)
    find_library(Vulkan_LIBRARY NAMES vulkan HINTS "$ENV{VULKAN_SDK}/lib" "${CMAKE_SOURCE_DIR}/libs/vulkan" REQUIRED)
    IF(Vulkan_LIBRARY)
        set(Vulkan_FOUND ON)
        MESSAGE("Using bundled Vulkan library version")
    ENDIF()
ENDIF()

# check if vulkan was found
IF (NOT Vulkan_FOUND)
	message(FATAL_ERROR "Could not find Vulkan library!")
ELSE()
	message(STATUS ${Vulkan_LIBRARY})
ENDIF()

# find windowing service
find_package(XCB REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_XCB_KHR")

# link sources
link_libraries(${XCB_LIBRARIES} ${Vulkan_LIBRARY})

add_subdirectory(photon)
