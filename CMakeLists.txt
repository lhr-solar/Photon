#/CMakeLists.txt
cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/find")

set(NAME Photon)
project(NAME VERSION 1.0)

if (NOT DEFINED VULKAN_SDK AND DEFINED ENV{VULKAN_SDK})
    set(VULKAN_SDK $ENV{VULKAN_SDK})
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(_photon_release_opt_flag "")
set(_photon_is_msvc_like FALSE)

if (MSVC)
    set(_photon_is_msvc_like TRUE)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if (CMAKE_CXX_COMPILER MATCHES "clang-cl" OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        set(_photon_is_msvc_like TRUE)
    endif()
endif()

if (_photon_is_msvc_like)
    add_compile_options(/Oy-)
    set(_photon_release_opt_flag "/O2 /Oi /Gy /GF")
else()
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        add_compile_options(-fno-omit-frame-pointer)
        set(_photon_release_opt_flag "-O3")
    endif()
endif()

if (_photon_release_opt_flag)
    foreach(_lang C CXX)
        set(_var "CMAKE_${_lang}_FLAGS_RELEASE")
        set(${_var} "${${_var}} ${_photon_release_opt_flag}" CACHE STRING "" FORCE)
    endforeach()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if (MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(_photon_release_opt_flag "-O3")
    foreach(_lang C CXX)
        set(_var "CMAKE_${_lang}_FLAGS_RELEASE")
        set(${_var} "${${_var}} ${_photon_release_opt_flag}" CACHE STRING "" FORCE)
    endforeach()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

set(_photon_output_root ${CMAKE_BINARY_DIR}/photon)
if (CMAKE_CONFIGURATION_TYPES)
    foreach(_cfg ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${_cfg} _cfg_upper)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg_upper} ${_photon_output_root}/${_cfg})
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg_upper} ${_photon_output_root}/${_cfg})
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_cfg_upper} ${_photon_output_root}/${_cfg})
    endforeach()
else()
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type")
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
    endif()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${_photon_output_root}/${CMAKE_BUILD_TYPE})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${_photon_output_root}/${CMAKE_BUILD_TYPE})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${_photon_output_root}/${CMAKE_BUILD_TYPE})
endif()

if (WIN32)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
endif()

include_directories(.external)
include_directories(.external/glm)
include_directories(.external/vulkan)
include_directories(.external/vk_video)
include_directories(.external/stb)
include_directories(.external/imgui)
include_directories(.external/imgui/backends)
include_directories(.external/tinygltf)
include_directories(${CMAKE_BINARY_DIR}/generated)

# Surface only platform-compatible vendored SDL3 packages from .external.
set(_photon_sdl3_vendor_root "${CMAKE_SOURCE_DIR}/.external")
if (EXISTS "${_photon_sdl3_vendor_root}")
    file(GLOB _photon_sdl3_prefixes
        LIST_DIRECTORIES true
        "${_photon_sdl3_vendor_root}/SDL3"
        "${_photon_sdl3_vendor_root}/SDL3-*")
    foreach(_photon_sdl3_prefix ${_photon_sdl3_prefixes})
        set(_photon_sdl3_is_native FALSE)
        if (WIN32)
            if (EXISTS "${_photon_sdl3_prefix}/lib/SDL3-static.lib" OR EXISTS "${_photon_sdl3_prefix}/lib/SDL3.lib")
                set(_photon_sdl3_is_native TRUE)
            endif()
        else()
            if (EXISTS "${_photon_sdl3_prefix}/lib/libSDL3.a" OR EXISTS "${_photon_sdl3_prefix}/lib/libSDL3.so")
                set(_photon_sdl3_is_native TRUE)
            endif()
        endif()

        if (_photon_sdl3_is_native)
            if (EXISTS "${_photon_sdl3_prefix}/cmake/SDL3Config.cmake")
                list(APPEND CMAKE_PREFIX_PATH "${_photon_sdl3_prefix}/cmake")
            elseif (EXISTS "${_photon_sdl3_prefix}/lib/cmake/SDL3/SDL3Config.cmake")
                list(APPEND CMAKE_PREFIX_PATH "${_photon_sdl3_prefix}/lib/cmake/SDL3")
            endif()
        endif()
    endforeach()
endif()

# require python
find_package(Python3 REQUIRED COMPONENTS Interpreter)

option(PHOTON_REQUIRE_STATIC_RELEASE "Fail configuration if a Release build would link against dynamic third-party libraries" OFF)
option(PHOTON_REQUIRE_STATIC_SDL "Require SDL to be linked statically so the runtime does not depend on libSDL3 being installed" ON)

# find SDL3 for the GPU module
find_package(SDL3 QUIET CONFIG)
unset(PHOTON_SDL_TARGET)

if (WIN32)
    if (TARGET SDL3::SDL3-static)
        set(PHOTON_SDL_TARGET SDL3::SDL3-static)
    elseif (TARGET SDL3::SDL3)
        set(PHOTON_SDL_TARGET SDL3::SDL3)
    else()
        message(FATAL_ERROR "SDL3 was not found. Provide a Windows SDL3 package in .external or point SDL3_DIR to one.")
    endif()
else()
    set(_photon_sdl_target_from_config "")
    if (TARGET SDL3::SDL3-static)
        set(_photon_sdl_target_from_config SDL3::SDL3-static)
    elseif (TARGET SDL3::SDL3)
        set(_photon_sdl_target_from_config SDL3::SDL3)
    endif()

    set(_photon_sdl_import_location "")
    if (_photon_sdl_target_from_config)
        get_target_property(_photon_sdl_import_location "${_photon_sdl_target_from_config}" IMPORTED_LOCATION_RELEASE)
        if (NOT _photon_sdl_import_location)
            get_target_property(_photon_sdl_import_location "${_photon_sdl_target_from_config}" IMPORTED_LOCATION_RELWITHDEBINFO)
        endif()
        if (NOT _photon_sdl_import_location)
            get_target_property(_photon_sdl_import_location "${_photon_sdl_target_from_config}" IMPORTED_LOCATION_DEBUG)
        endif()
        if (NOT _photon_sdl_import_location)
            get_target_property(_photon_sdl_import_location "${_photon_sdl_target_from_config}" IMPORTED_LOCATION)
        endif()
    endif()

    if (_photon_sdl_import_location MATCHES "\\.a$")
        set(PHOTON_SDL_TARGET "${_photon_sdl_target_from_config}")
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL3 REQUIRED sdl3)

        set(_photon_sdl_static_archive "")
        foreach(_photon_sdl_libdir IN LISTS SDL3_LIBRARY_DIRS)
            if (EXISTS "${_photon_sdl_libdir}/libSDL3.a")
                set(_photon_sdl_static_archive "${_photon_sdl_libdir}/libSDL3.a")
                break()
            endif()
        endforeach()

        if (_photon_sdl_static_archive)
            execute_process(
                COMMAND pkg-config --libs --static sdl3
                OUTPUT_VARIABLE _photon_sdl_static_libs_raw
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            execute_process(
                COMMAND pkg-config --cflags sdl3
                OUTPUT_VARIABLE _photon_sdl_cflags_raw
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            separate_arguments(_photon_sdl_static_libs NATIVE_COMMAND "${_photon_sdl_static_libs_raw}")
            separate_arguments(_photon_sdl_cflags NATIVE_COMMAND "${_photon_sdl_cflags_raw}")

            set(_photon_sdl_extra_link_items "")
            foreach(_photon_sdl_item IN LISTS _photon_sdl_static_libs)
                if (NOT _photon_sdl_item STREQUAL "-lSDL3")
                    list(APPEND _photon_sdl_extra_link_items "${_photon_sdl_item}")
                endif()
            endforeach()

            add_library(photon_sdl3_static INTERFACE)
            target_include_directories(photon_sdl3_static SYSTEM INTERFACE ${SDL3_INCLUDE_DIRS})
            target_compile_options(photon_sdl3_static INTERFACE ${_photon_sdl_cflags})
            target_link_libraries(photon_sdl3_static INTERFACE "${_photon_sdl_static_archive}" ${_photon_sdl_extra_link_items})
            set(PHOTON_SDL_TARGET photon_sdl3_static)
        elseif (PHOTON_REQUIRE_STATIC_SDL)
            message(FATAL_ERROR
                "A native static SDL3 library was not found for Linux. "
                "Provide libSDL3.a (via a Linux SDL3 static package or vendored Linux build), "
                "or configure with -DPHOTON_REQUIRE_STATIC_SDL=OFF to allow dynamic linking.")
        else()
            pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
            set(PHOTON_SDL_TARGET PkgConfig::SDL3)
            message(WARNING "Falling back to dynamic SDL3 on Linux because libSDL3.a was not found.")
        endif()
    endif()
endif()

if (PHOTON_REQUIRE_STATIC_RELEASE AND NOT CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _photon_build_type_upper)
    if (_photon_build_type_upper STREQUAL "RELEASE")
        set(_photon_known_static_targets SDL3::SDL3-static photon_sdl3_static)
        if (NOT PHOTON_SDL_TARGET IN_LIST _photon_known_static_targets)
            message(FATAL_ERROR "Release builds require static third-party libraries. Provide SDL3::SDL3-static (or disable PHOTON_REQUIRE_STATIC_RELEASE).")
        endif()
    endif()
endif()

# find Vulkan
IF(WIN32)
	IF (NOT Vulkan_FOUND)
		find_library(Vulkan_LIBRARY NAMES vulkan-1 vulkan PATHS ${CMAKE_SOURCE_DIR}/libs/vulkan)
		IF (Vulkan_LIBRARY)
			set(Vulkan_FOUND ON)
			MESSAGE("Using bundled Vulkan library version")
		ENDIF()
	ENDIF()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR -DWIN")
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
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_XCB_KHR -DLINUX")
ENDIF()

# check if vulkan was found
IF (NOT Vulkan_FOUND)
	message(FATAL_ERROR "Could not find Vulkan library!")
ELSE()
	message(STATUS ${Vulkan_LIBRARY})
ENDIF()

# link sources
IF(WIN32)
	link_libraries(${Vulkan_LIBRARY})
ELSE()
    link_libraries(${XCB_LIBRARIES} ${Vulkan_LIBRARY})
ENDIF(WIN32)

add_subdirectory(assets/kernels)
add_subdirectory(assets/fonts)
add_subdirectory(assets/dbc)
add_subdirectory(assets/models)
add_subdirectory(photon)
