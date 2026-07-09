file(GLOB gui_src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM gui_src "${CMAKE_CURRENT_SOURCE_DIR}/hot_reload.cpp")

if (APPLE)
    list(REMOVE_ITEM gui_src "${CMAKE_CURRENT_SOURCE_DIR}/gpuGui.cpp")
else()
    list(REMOVE_ITEM gui_src
        "${CMAKE_CURRENT_SOURCE_DIR}/gpuMetalGui.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/stb_image_impl.cpp"
    )
endif()

file(GLOB ddash_src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/DDash/*.cpp")
list(APPEND gui_src ${ddash_src})

add_library(photon_http INTERFACE)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(CURL REQUIRED)
    target_compile_definitions(photon_http INTERFACE PHOTON_HAS_CURL=1)
    target_link_libraries(photon_http INTERFACE CURL::libcurl)
endif()

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
    photon_http
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
        photon_http
    )

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(photonUI PRIVATE -Wl,-Bsymbolic)
    endif()
endif()
