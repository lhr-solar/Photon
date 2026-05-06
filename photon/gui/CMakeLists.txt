file(GLOB gui_src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM gui_src "${CMAKE_CURRENT_SOURCE_DIR}/hot_reload.cpp")

file(GLOB imgui_runtime_src CONFIGURE_DEPENDS "../../.external/imgui/*.cpp" "../../.external/imgui/backends/*.cpp")
add_library(imgui_runtime SHARED ${imgui_runtime_src})
set_target_properties(imgui_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
target_include_directories(imgui_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(imgui_runtime
    PUBLIC
    ${PHOTON_SDL_TARGET}
    EnvironmentHeaders
    EnvironmentAssets
)

add_library(gui ${gui_src})
set_target_properties(gui PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(gui
    PUBLIC
    imgui_runtime
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(imgui_runtime PUBLIC v4l2)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_library(photonUI SHARED hot_reload.cpp)
    target_include_directories(photonUI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
    target_link_libraries(photonUI PRIVATE gui)
endif()
