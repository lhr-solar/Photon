cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

if(NOT DEFINED CMAKE_BUILD_TYPE AND NOT DEFINED CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED CMAKE_C_COMPILER)
    find_program(CMAKE_C_COMPILER clang REQUIRED)
endif()

if(NOT DEFINED CMAKE_CXX_COMPILER)
    find_program(CMAKE_CXX_COMPILER clang++ REQUIRED)
endif()

project(Photon VERSION 1.0 LANGUAGES C CXX)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/find")

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(WIN32)
        message(FATAL_ERROR
            "Photon requires clang.\n"
            "\n"
            "On Windows, configure a fresh build directory with clang explicitly selected, for example:\n"
            "  cmake -S . -B .artifacts -G Ninja -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++\n"
        )
    else()
        message(FATAL_ERROR
            "Photon requires clang.\n"
            "\n"
            "Configure a fresh build directory with clang explicitly selected, for example:\n"
            "  cmake -S . -B build -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++"
        )
    endif()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

include(ProcessorCount)
ProcessorCount(PHOTON_BUILD_JOBS)
if(NOT PHOTON_BUILD_JOBS)
    set(PHOTON_BUILD_JOBS 1)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(WIN32)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS VK_USE_PLATFORM_WIN32_KHR WIN)
else()
    add_compile_definitions(VK_USE_PLATFORM_XCB_KHR LINUX)
endif()

add_compile_options(-fno-omit-frame-pointer)
if(NOT WIN32)
    add_link_options(-rdynamic)
endif()

include_directories(
    .external
    .external/glm
    .external/vulkan
    .external/vk_video
    .external/stb
    .external/imgui
    .external/imgui/backends
    .external/tinygltf
    ${CMAKE_BINARY_DIR}/generated
)

option(PHOTON_TRACY "" OFF)
set(TRACY_ENABLE ${PHOTON_TRACY} CACHE BOOL "" FORCE)
set(TRACY_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(.external/tracy EXCLUDE_FROM_ALL)

if(NOT DEFINED VULKAN_SDK AND DEFINED ENV{VULKAN_SDK})
    set(VULKAN_SDK "$ENV{VULKAN_SDK}")
endif()

set(_photon_sdl_vendor_root "${CMAKE_SOURCE_DIR}/.external")
if(EXISTS "${_photon_sdl_vendor_root}")
    file(GLOB _photon_sdl_prefixes
        LIST_DIRECTORIES true
        "${_photon_sdl_vendor_root}/SDL3"
        "${_photon_sdl_vendor_root}/SDL3-*"
    )
    foreach(_photon_sdl_prefix IN LISTS _photon_sdl_prefixes)
        set(_photon_sdl_native FALSE)
        if(WIN32)
            if(EXISTS "${_photon_sdl_prefix}/lib/SDL3-static.lib" OR EXISTS "${_photon_sdl_prefix}/lib/SDL3.lib")
                set(_photon_sdl_native TRUE)
            endif()
        else()
            if(EXISTS "${_photon_sdl_prefix}/lib/libSDL3.a" OR EXISTS "${_photon_sdl_prefix}/lib/libSDL3.so")
                set(_photon_sdl_native TRUE)
            endif()
        endif()
        if(_photon_sdl_native AND EXISTS "${_photon_sdl_prefix}/cmake/SDL3Config.cmake")
            list(APPEND CMAKE_PREFIX_PATH "${_photon_sdl_prefix}/cmake")
        elseif(_photon_sdl_native AND EXISTS "${_photon_sdl_prefix}/lib/cmake/SDL3/SDL3Config.cmake")
            list(APPEND CMAKE_PREFIX_PATH "${_photon_sdl_prefix}/lib/cmake/SDL3")
        endif()
    endforeach()
endif()

find_package(SDL3 QUIET CONFIG)
unset(PHOTON_SDL_TARGET)

if(WIN32)
    if(TARGET SDL3::SDL3-static)
        set(PHOTON_SDL_TARGET SDL3::SDL3-static)
    elseif(TARGET SDL3::SDL3)
        set(PHOTON_SDL_TARGET SDL3::SDL3)
    else()
        message(FATAL_ERROR "SDL3 was not found.")
    endif()
else()
    if(TARGET SDL3::SDL3-static)
        set(PHOTON_SDL_TARGET SDL3::SDL3-static)
    elseif(TARGET SDL3::SDL3)
        get_target_property(_photon_sdl_location SDL3::SDL3 IMPORTED_LOCATION)
        if(_photon_sdl_location MATCHES "\\.a$")
            set(PHOTON_SDL_TARGET SDL3::SDL3)
        endif()
    endif()

    if(NOT PHOTON_SDL_TARGET)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL3 REQUIRED sdl3)

        foreach(_photon_sdl_libdir IN LISTS SDL3_LIBRARY_DIRS)
            if(EXISTS "${_photon_sdl_libdir}/libSDL3.a")
                set(_photon_sdl_archive "${_photon_sdl_libdir}/libSDL3.a")
                break()
            endif()
        endforeach()

        if(_photon_sdl_archive)
            execute_process(
                COMMAND pkg-config --libs --static sdl3
                OUTPUT_VARIABLE _photon_sdl_libs_raw
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            execute_process(
                COMMAND pkg-config --cflags sdl3
                OUTPUT_VARIABLE _photon_sdl_cflags_raw
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            separate_arguments(_photon_sdl_libs NATIVE_COMMAND "${_photon_sdl_libs_raw}")
            separate_arguments(_photon_sdl_cflags NATIVE_COMMAND "${_photon_sdl_cflags_raw}")
            add_library(photon_sdl3_static INTERFACE)
            target_include_directories(photon_sdl3_static SYSTEM INTERFACE ${SDL3_INCLUDE_DIRS})
            target_compile_options(photon_sdl3_static INTERFACE ${_photon_sdl_cflags})
            foreach(_photon_sdl_lib IN LISTS _photon_sdl_libs)
                if(NOT _photon_sdl_lib STREQUAL "-lSDL3")
                    target_link_libraries(photon_sdl3_static INTERFACE "${_photon_sdl_lib}")
                endif()
            endforeach()
            target_link_libraries(photon_sdl3_static INTERFACE "${_photon_sdl_archive}")
            set(PHOTON_SDL_TARGET photon_sdl3_static)
        elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
            message(FATAL_ERROR "Release builds require a static SDL3 library.")
        else()
            pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
            set(PHOTON_SDL_TARGET PkgConfig::SDL3)
        endif()
    endif()
endif()

if(WIN32)
    set(_photon_vulkan_hints
        "${VULKAN_SDK}"
        "$ENV{VULKAN_SDK}"
        "$ENV{VK_SDK_PATH}"
        "${CMAKE_SOURCE_DIR}/libs/vulkan"
    )
    find_library(Vulkan_LIBRARY
        NAMES vulkan-1 vulkan
        HINTS ${_photon_vulkan_hints}
        PATH_SUFFIXES Lib lib
    )
else()
    find_library(Vulkan_LIBRARY NAMES vulkan HINTS "$ENV{VULKAN_SDK}/lib" "${CMAKE_SOURCE_DIR}/libs/vulkan" REQUIRED)
    find_package(XCB REQUIRED)
endif()

if(NOT Vulkan_LIBRARY)
    if(WIN32)
        message(FATAL_ERROR
            "Could not find Vulkan.\n"
            "\n"
            "Checked these Windows Vulkan SDK locations:\n"
            "  VULKAN_SDK='${VULKAN_SDK}'\n"
            "  ENV{VULKAN_SDK}='$ENV{VULKAN_SDK}'\n"
            "  ENV{VK_SDK_PATH}='$ENV{VK_SDK_PATH}'\n"
            "  '${CMAKE_SOURCE_DIR}/libs/vulkan'\n"
            "\n"
            "Expected to find vulkan-1.lib under a Lib directory, for example:\n"
            "  C:/VulkanSDK/<version>/Lib/vulkan-1.lib"
        )
    else()
        message(FATAL_ERROR "Could not find Vulkan.")
    endif()
endif()

message(STATUS "${Vulkan_LIBRARY}")

if(WIN32)
    link_libraries("${Vulkan_LIBRARY}")
else()
    link_libraries(${XCB_LIBRARIES} "${Vulkan_LIBRARY}")
endif()

set(PHOTON_RELEASE_BINARY_DIR "${CMAKE_SOURCE_DIR}/build-release")
add_custom_target(release
    COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_SOURCE_DIR}" -B "${PHOTON_RELEASE_BINARY_DIR}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    COMMAND "${CMAKE_COMMAND}" --build "${PHOTON_RELEASE_BINARY_DIR}" --parallel "${PHOTON_BUILD_JOBS}"
    USES_TERMINAL
)

add_custom_target(tracy
    COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_SOURCE_DIR}" -B "${CMAKE_BINARY_DIR}" -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DPHOTON_TRACY=ON
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --parallel "${PHOTON_BUILD_JOBS}"
    USES_TERMINAL
)

add_custom_target(notracy
    COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_SOURCE_DIR}" -B "${CMAKE_BINARY_DIR}" -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DPHOTON_TRACY=OFF
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --parallel "${PHOTON_BUILD_JOBS}"
    USES_TERMINAL
)

add_subdirectory(assets/kernels)
add_subdirectory(assets/fonts)
add_subdirectory(assets/dbc)
add_subdirectory(assets/environments)
add_subdirectory(assets/models)
add_subdirectory(photon)
