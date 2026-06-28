file(GLOB gui_src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM gui_src "${CMAKE_CURRENT_SOURCE_DIR}/hot_reload.cpp")

add_library(gui ${gui_src})
set_target_properties(gui PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(gui
    PUBLIC
    ${PHOTON_SDL_TARGET}
    EnvironmentHeaders
    EnvironmentAssets
    imgui_runtime
    gpu
    network
    parse
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(gui PUBLIC v4l2)
endif()

if (PHOTON_UI_HOT_RELOAD)
    add_library(photonUI SHARED hot_reload.cpp ${gui_src})
    target_include_directories(photonUI PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/generated
    )
    target_link_libraries(photonUI PRIVATE
        ${PHOTON_SDL_TARGET}
        EnvironmentHeaders
        EnvironmentAssets
        imgui_runtime
        gpu
        network
        parse
    )

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(photonUI PRIVATE -Wl,-Bsymbolic)
    endif()
endif()
