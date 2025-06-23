#photon/CMakeLists.txt

cmake_minimum_required(VERSION 3.4 FATAL_ERROR)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

set(NAME photon)

project(${NAME})

if (MSVC)
	foreach(_flag_var
		CMAKE_C_FLAGS_RELEASE
		CMAKE_C_FLAGS_RELWITHDEBINFO
		CMAKE_C_FLAGS_MINSIZEREL
		CMAKE_C_FLAGS_DEBUG
		CMAKE_CXX_FLAGS_RELEASE
		CMAKE_CXX_FLAGS_RELWITHDEBINFO
		CMAKE_CXX_FLAGS_MINSIZEREL
		CMAKE_CXX_FLAGS_DEBUG)
		string(REPLACE "/MD"  "/MT"  ${_flag_var} "${${_flag_var}}")
		string(REPLACE "/MDd" "/MTd" ${_flag_var} "${${_flag_var}}")
	endforeach()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
# at the very top, right after project()
if (WIN32)
  add_compile_definitions(
    WIN32_LEAN_AND_MEAN  # strip out winsock.h from windows.h
    NOMINMAX            # your existing define
    _USE_MATH_DEFINES   # your existing define
  )
endif()


include_directories(external)
include_directories(external/glm)
include_directories(external/imgui)
include_directories(external/tinygltf)
include_directories(external/ktx/include)
include_directories(external/ktx/other_include)
include_directories(base)
include_directories(${CMAKE_BINARY_DIR}/generated)

IF(UNIX AND NOT APPLE)
	set(LINUX TRUE)
ENDIF()


# OS Specific find Vulkan
IF(WIN32)
	IF (NOT Vulkan_FOUND)
		find_library(Vulkan_LIBRARY NAMES vulkan-1 vulkan PATHS ${CMAKE_SOURCE_DIR}/libs/vulkan)
		IF (Vulkan_LIBRARY)
			set(Vulkan_FOUND ON)
			MESSAGE("Using bundled Vulkan library version")
		ENDIF()
	ENDIF()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
ELSEIF(LINUX)
	IF (NOT Vulkan_FOUND)
		find_library(Vulkan_LIBRARY NAMES vulkan HINTS "$ENV{VULKAN_SDK}/lib" "${CMAKE_SOURCE_DIR}/libs/vulkan" REQUIRED)
		IF (Vulkan_LIBRARY)
			set(Vulkan_FOUND ON)
			MESSAGE("Using bundled Vulkan library version")
		ENDIF()
	ENDIF()
	find_package(Threads REQUIRED)
	IF(USE_D2D_WSI)
		MESSAGE("Using direct to display extension...")
		add_definitions(-D_DIRECT2DISPLAY)
	ELSEIF(USE_DIRECTFB_WSI)
		find_package(DirectFB REQUIRED)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_DIRECTFB_EXT")
		include_directories(${DIRECTFB_INCLUDE_DIR})
	ELSEIF(USE_WAYLAND_WSI)
		find_program(PKG_CONFIG pkg-config)
		if (NOT PKG_CONFIG)
			message(FATAL_ERROR "pkg-config binary not found")
		endif ()
		find_package(Wayland REQUIRED)
		if (NOT WAYLAND_FOUND)
			message(FATAL_ERROR "Wayland development package not found")
		endif ()
		pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
		if (NOT WAYLAND_PROTOCOLS_FOUND)
			message(FATAL_ERROR "Wayland protocols package not found")
		endif ()
		find_program(WAYLAND_SCANNER wayland-scanner)
		if (NOT WAYLAND_SCANNER)
			message(FATAL_ERROR "wayland-scanner binary not found")
		endif ()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WAYLAND_KHR")
		include_directories(${WAYLAND_INCLUDE_DIR})
		pkg_get_variable(protocol_dir wayland-protocols pkgdatadir)
		execute_process(COMMAND ${WAYLAND_SCANNER} client-header ${protocol_dir}/stable/xdg-shell/xdg-shell.xml ${CMAKE_BINARY_DIR}/xdg-shell-client-protocol.h
				COMMAND ${WAYLAND_SCANNER} private-code ${protocol_dir}/stable/xdg-shell/xdg-shell.xml ${CMAKE_BINARY_DIR}/xdg-shell-protocol.c)
		include_directories(${CMAKE_BINARY_DIR})
	ELSEIF(USE_HEADLESS)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_HEADLESS_EXT")
	ELSE(USE_D2D_WSI)
		find_package(XCB REQUIRED)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_XCB_KHR")
	ENDIF(USE_D2D_WSI)
ENDIF(WIN32)

# Vulkan Not found
IF (NOT Vulkan_FOUND)
	message(FATAL_ERROR "Could not find Vulkan library!")
ELSE()
	message(STATUS ${Vulkan_LIBRARY})
ENDIF()

# Set preprocessor defines
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNOMINMAX -D_USE_MATH_DEFINES")

# Clang specific stuff
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-switch-enum")
endif()


add_definitions(-D_CRT_SECURE_NO_WARNINGS)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SOURCE *.cpp )

# Asset and shader path selection
if(RESOURCE_INSTALL_DIR)
	add_definitions(-DVK_EXAMPLE_ASSETS_DIR=\"${RESOURCE_INSTALL_DIR}/\")
	add_definitions(-DVK_EXAMPLE_SHADERS_DIR=\"${RESOURCE_INSTALL_DIR}/shaders/\")
	install(DIRECTORY assets/ DESTINATION ${RESOURCE_INSTALL_DIR}/)
	install(DIRECTORY shaders/ DESTINATION ${RESOURCE_INSTALL_DIR}/shaders/)
else()
	if(NOT USE_RELATIVE_ASSET_PATH)
        #		add_definitions(-DVK_EXAMPLE_ASSETS_DIR=\"${CMAKE_SOURCE_DIR}/assets/\")
        #		add_definitions(-DVK_EXAMPLE_SHADERS_DIR=\"${CMAKE_SOURCE_DIR}/shaders/\")

        add_definitions(-DVK_EXAMPLE_ASSETS_DIR=\"assets/\")
        add_definitions(-DVK_EXAMPLE_SHADERS_DIR=\"shaders/\")
            
        #add_definitions(-DVK_EXAMPLE_ASSETS_DIR=\"~\")
        #add_definitions(-DVK_EXAMPLE_SHADERS_DIR=\"~\")

	endif()
endif()

# Force validation at compile time
if (FORCE_VALIDATION)
	add_definitions(-DFORCE_VALIDATION)
endif()

# Compiler specific stuff
IF(MSVC)
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
ELSEIF(APPLE)
	IF(CMAKE_GENERATOR MATCHES "Makefiles" OR CMAKE_GENERATOR MATCHES "Ninja")
		SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-arc -ObjC++")
	ELSE() # SRS - use objective-c++ language flag vs. ObjC++ for Xcode builds
		SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-arc -xobjective-c++")
	ENDIF()
ENDIF(MSVC)

IF(WIN32)
	# Nothing here (yet)
ELSEIF(APPLE)
	link_libraries(${Vulkan_LIBRARY} "-framework AppKit" "-framework QuartzCore")
ELSE(WIN32)
	link_libraries(${XCB_LIBRARIES} ${Vulkan_LIBRARY} ${Vulkan_LIBRARY} ${DIRECTFB_LIBRARIES} ${WAYLAND_CLIENT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
ENDIF(WIN32)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")

# Generate shader headers from SPIR-V binaries
set(SPV_SHADERS
    shaders/glsl/imgui/scene.vert.spv
    shaders/glsl/imgui/scene.frag.spv
    shaders/glsl/imgui/ui.vert.spv
    shaders/glsl/imgui/ui.frag.spv
    shaders/glsl/custom/custom_model.vert.spv
    shaders/glsl/custom/custom_model.frag.spv
    shaders/glsl/custom/particle_system.vert.spv
    shaders/glsl/custom/particle_system.frag.spv
    shaders/glsl/custom/particles.vert.spv
    shaders/glsl/custom/particles.frag.spv
)

set(GENERATED_SHADER_HEADERS)
foreach(SPV ${SPV_SHADERS})
    get_filename_component(NAME ${SPV} NAME)
    string(REPLACE "." "_" VAR ${NAME})
    set(HEADER ${CMAKE_BINARY_DIR}/generated/${VAR}.hpp)
    add_custom_command(
        OUTPUT ${HEADER}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/spv_to_header.py ${CMAKE_SOURCE_DIR}/${SPV} ${HEADER}
        DEPENDS ${SPV}
        COMMENT "Generating ${HEADER} from ${SPV}")
    list(APPEND GENERATED_SHADER_HEADERS ${HEADER})
endforeach()
add_custom_target(GenerateShaderHeaders ALL DEPENDS ${GENERATED_SHADER_HEADERS})

# Generate font headers from TTF files
set(FONTS
    fonts/Inter.ttf
)
set(GENERATED_FONT_HEADERS)
foreach(FONT ${FONTS})
    get_filename_component(FNAME ${FONT} NAME)
    string(REPLACE "." "_" FVAR ${FNAME})
    set(FHEADER ${CMAKE_BINARY_DIR}/generated/${FVAR}.hpp)
    add_custom_command(
        OUTPUT ${FHEADER}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/file_to_header.py ${CMAKE_SOURCE_DIR}/${FONT} ${FHEADER}
        DEPENDS ${FONT}
        COMMENT "Generating ${FHEADER} from ${FONT}")
    list(APPEND GENERATED_FONT_HEADERS ${FHEADER})
endforeach()
add_custom_target(GenerateFontHeaders ALL DEPENDS ${GENERATED_FONT_HEADERS})

# Generate dbc headers
set(DBC_FILES
    dbc/bps.dbc
    dbc/controls.dbc
    dbc/prohelion_wavesculptor22.dbc
    dbc/tpee_mppt[B].dbc
    dbc/tpee_mppt[A].dbc
    dbc/daq.dbc
)
set(GENERATED_DBC_HEADERS)
foreach(DBC ${DBC_FILES})
    get_filename_component(DNAME ${DBC} NAME)
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" DVAR ${DNAME})
    set(DHEADER ${CMAKE_BINARY_DIR}/generated/${DVAR}.hpp)
    add_custom_command(
        OUTPUT ${DHEADER}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/file_to_header.py ${CMAKE_SOURCE_DIR}/${DBC} ${DHEADER}
        DEPENDS ${DBC}
        COMMENT "Generating ${DHEADER} from ${DBC}")
    list(APPEND GENERATED_DBC_HEADERS ${DHEADER})
endforeach()
add_custom_target(GenerateDbcHeaders ALL DEPENDS ${GENERATED_DBC_HEADERS})

add_subdirectory(base)
add_dependencies(base GenerateShaderHeaders GenerateFontHeaders GenerateDbcHeaders)
add_subdirectory(core)
add_dependencies(core GenerateShaderHeaders GenerateFontHeaders GenerateDbcHeaders)
