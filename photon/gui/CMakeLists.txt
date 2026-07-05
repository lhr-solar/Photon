file(GLOB gui_src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM gui_src "${CMAKE_CURRENT_SOURCE_DIR}/hot_reload.cpp")

find_package(CURL QUIET)
add_library(libcurl INTERFACE)
if (CURL_FOUND)
    target_compile_definitions(libcurl INTERFACE PHOTON_HAS_CURL=1)
    target_link_libraries(libcurl INTERFACE CURL::libcurl)
else()
    message(WARNING "libcurl not found; update release checks are disabled.")
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
    libcurl 
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
        libcurl
    )

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(photonUI PRIVATE -Wl,-Bsymbolic)
    endif()
endif()
