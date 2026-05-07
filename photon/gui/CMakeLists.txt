file(GLOB src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "../../.external/imgui/*.cpp" "../../.external/imgui/backends/*.cpp")
list(REMOVE_ITEM src "${CMAKE_CURRENT_SOURCE_DIR}/hot_reload.cpp")
add_library(gui ${src})
set_target_properties(gui PROPERTIES POSITION_INDEPENDENT_CODE ON)

file(GLOB gui_hot_reload_src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM gui_hot_reload_src "${CMAKE_CURRENT_SOURCE_DIR}/hot_reload.cpp")

target_include_directories(gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(gui PUBLIC ${PHOTON_SDL_TARGET} EnvironmentHeaders EnvironmentAssets)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(gui PUBLIC v4l2)
endif()

if ((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32) AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_library(photonUI SHARED hot_reload.cpp ${gui_hot_reload_src})
    target_include_directories(photonUI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(photonUI PRIVATE -Wl,-Bsymbolic)
    elseif (WIN32)
        target_link_libraries(photonUI
            PRIVATE
            ${PHOTON_SDL_TARGET} EnvironmentHeaders EnvironmentAssets
        )
    endif()
endif()
