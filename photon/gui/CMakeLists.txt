file(GLOB src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "../../.external/imgui/*.cpp" "../../.external/imgui/backends/*.cpp")
list(REMOVE_ITEM src "${CMAKE_CURRENT_SOURCE_DIR}/hot_reload.cpp")
add_library(gui ${src})
set_target_properties(gui PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(gui
    PUBLIC
    ${PHOTON_SDL_TARGET}
    EnvironmentHeaders
    EnvironmentAssets
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(gui PUBLIC v4l2)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_library(photonUI SHARED hot_reload.cpp)
    target_include_directories(photonUI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
    target_link_libraries(photonUI PRIVATE gui)
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(photonUI PRIVATE -Wl,-Bsymbolic)
    endif()
endif()
