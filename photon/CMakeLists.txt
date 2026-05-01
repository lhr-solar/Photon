include(${CMAKE_SOURCE_DIR}/find/embed_assets.cmake)

add_subdirectory(engine)
add_subdirectory(gpu)
add_subdirectory(gui)
add_subdirectory(network)
add_subdirectory(parse)
add_subdirectory(synth)

add_executable(Photon engine/main.cpp)
set_target_properties(Photon PROPERTIES ENABLE_EXPORTS ON)
photon_attach_windows_resources(Photon)

if (WIN32)
    target_sources(Photon PRIVATE ../assets/fonts/Photon.rc)
    if (MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        set_target_properties(Photon PROPERTIES
            LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
            LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
            LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
    endif()
endif()

target_link_libraries(Photon
    PRIVATE
    engine
    $<$<PLATFORM_ID:Linux>:dl>
)

if (TARGET photonUI)
    target_compile_definitions(engine PRIVATE
        PHOTON_UI_SO_PATH="$<TARGET_FILE:photonUI>"
        PHOTON_UI_DIR_PATH="${CMAKE_SOURCE_DIR}/photon/gui"
        PHOTON_BUILD_DIR="${CMAKE_BINARY_DIR}"
    )
endif()
