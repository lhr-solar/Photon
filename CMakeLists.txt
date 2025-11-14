#/CMakeLists.txt
cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/find")

set(NAME Photon)
project(NAME VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------
# Include directories
# ------------------------------
include_directories(.external)
include_directories(.external/glm)
include_directories(.external/vulkan)
include_directories(.external/vk_video)
include_directories(.external/stb)
include_directories(.external/imgui)
include_directories(.external/tinygltf)
include_directories(${CMAKE_BINARY_DIR}/generated)

# ------------------------------
# Python
# ------------------------------
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ------------------------------
# Vulkan
# ------------------------------
IF(WIN32)
	IF (NOT Vulkan_FOUND)
		find_library(Vulkan_LIBRARY NAMES vulkan-1 vulkan PATHS ${CMAKE_SOURCE_DIR}/libs/vulkan)
		IF (Vulkan_LIBRARY)
			set(Vulkan_FOUND ON)
			MESSAGE("Using bundled Vulkan library version")
		ENDIF()
	ENDIF()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
ELSE()
	IF(NOT Vulkan_FOUND)
    	find_library(Vulkan_LIBRARY NAMES vulkan HINTS "$ENV{VULKAN_SDK}/lib" "${CMAKE_SOURCE_DIR}/libs/vulkan" REQUIRED)
    	IF(Vulkan_LIBRARY)
        	set(Vulkan_FOUND ON)
        	MESSAGE("Using bundled Vulkan library version")
    	ENDIF()
	ENDIF()
	# find windowing service
	find_package(XCB REQUIRED)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_XCB_KHR")
ENDIF()

IF (NOT Vulkan_FOUND)
	message(FATAL_ERROR "Could not find Vulkan library!")
ELSE()
	message(STATUS ${Vulkan_LIBRARY})
ENDIF()

IF(WIN32)
	link_libraries(${Vulkan_LIBRARY})
ELSE()
	link_libraries(${XCB_LIBRARIES} ${Vulkan_LIBRARY})
ENDIF(WIN32)

# ------------------------------
# FFmpeg Setup
# ------------------------------
set(FFMPEG_COMPONENTS avcodec avformat avutil swscale swresample)
set(FFMPEG_LIBRARIES "")

foreach(component ${FFMPEG_COMPONENTS})
    if(WIN32)
        find_library(FFMPEG_${component}_LIBRARY
            NAMES ${component} lib${component}
            HINTS
                C:/ffmpeg/lib
                ${CMAKE_SOURCE_DIR}/libs/ffmpeg/lib
        )
    else()
        find_library(FFMPEG_${component}_LIBRARY
            NAMES ${component} lib${component}
            HINTS
                /usr/lib /usr/local/lib ${CMAKE_SOURCE_DIR}/libs/ffmpeg/lib
        )
    endif()

    if(FFMPEG_${component}_LIBRARY)
        list(APPEND FFMPEG_LIBRARIES ${FFMPEG_${component}_LIBRARY})
        message(STATUS "[+] Found FFmpeg ${component}: ${FFMPEG_${component}_LIBRARY}")
    else()
        message(FATAL_ERROR "[!] FFmpeg library '${component}' not found!")
    endif()
endforeach()

find_path(FFMPEG_INCLUDE_DIR
    NAMES libavcodec/avcodec.h
    HINTS
        C:/ffmpeg/include
        /usr/include /usr/local/include
        ${CMAKE_SOURCE_DIR}/libs/ffmpeg/include
)
if(NOT FFMPEG_INCLUDE_DIR)
    message(FATAL_ERROR "[!] FFmpeg headers not found!")
else()
    message(STATUS "[+] Found FFmpeg headers at: ${FFMPEG_INCLUDE_DIR}")
    include_directories(${FFMPEG_INCLUDE_DIR})
endif()

# ------------------------------
# Subdirectories
# ------------------------------
add_subdirectory(kernels)
add_subdirectory(fonts)
add_subdirectory(photon)

# ------------------------------
# Windows compile definitions
# ------------------------------
if (WIN32)
    set_target_properties(Photon PROPERTIES WIN32_EXECUTABLE TRUE)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
endif()
