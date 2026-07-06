include(${CMAKE_SOURCE_DIR}/find/embed_assets.cmake)

set(PHOTON_UI_HOT_RELOAD OFF)
if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    set(PHOTON_UI_HOT_RELOAD ON)
endif()

file(GLOB imgui_runtime_src
    "../.external/imgui/*.cpp"
    "../.external/imgui/backends/*.cpp"
)

set(_photon_imgui_runtime_kind STATIC)
if (PHOTON_UI_HOT_RELOAD)
    set(_photon_imgui_runtime_kind SHARED)
endif()

add_library(imgui_runtime ${_photon_imgui_runtime_kind} ${imgui_runtime_src})
set_target_properties(imgui_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(imgui_runtime PUBLIC
    ${CMAKE_SOURCE_DIR}/.external/imgui
    ${CMAKE_SOURCE_DIR}/.external/imgui/backends
)

if (WIN32 AND PHOTON_UI_HOT_RELOAD)
    set_target_properties(imgui_runtime PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_compile_definitions(imgui_runtime
        PRIVATE
            IMGUI_API=__declspec\(dllexport\)
            IMPLOT_API=__declspec\(dllexport\)
            IMPLOT3D_API=__declspec\(dllexport\)
        INTERFACE
            IMGUI_API=__declspec\(dllimport\)
            IMPLOT_API=__declspec\(dllimport\)
            IMPLOT3D_API=__declspec\(dllimport\)
    )
endif()

add_subdirectory(gpu)
add_subdirectory(network)
add_subdirectory(parse)
add_subdirectory(synth)
add_subdirectory(gui)
add_subdirectory(engine)

add_executable(Photon engine/main.cpp)
set_target_properties(Photon PROPERTIES ENABLE_EXPORTS ON)
photon_attach_windows_resources(Photon)

if (WIN32)
    target_sources(Photon PRIVATE ../assets/fonts/Photon.rc)
    target_link_options(Photon PRIVATE
        $<$<CONFIG:Release>:LINKER:/SUBSYSTEM:WINDOWS>
        $<$<CONFIG:Release>:LINKER:/ENTRY:mainCRTStartup>
        $<$<CONFIG:RelWithDebInfo>:LINKER:/SUBSYSTEM:WINDOWS>
        $<$<CONFIG:RelWithDebInfo>:LINKER:/ENTRY:mainCRTStartup>
        $<$<CONFIG:MinSizeRel>:LINKER:/SUBSYSTEM:WINDOWS>
        $<$<CONFIG:MinSizeRel>:LINKER:/ENTRY:mainCRTStartup>
    )
endif()

target_link_libraries(Photon
    PRIVATE
    engine
    $<$<PLATFORM_ID:Linux>:dl>
)

if (PHOTON_UI_HOT_RELOAD)
    target_compile_definitions(engine PRIVATE
        PHOTON_UI_SO_PATH="$<TARGET_FILE:photonUI>"
        PHOTON_UI_DIR_PATH="${CMAKE_SOURCE_DIR}/photon/gui"
        PHOTON_BUILD_DIR="${CMAKE_BINARY_DIR}"
    )
endif()
