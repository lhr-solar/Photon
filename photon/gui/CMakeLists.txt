if (WIN32)
    file(GLOB gui_src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
    list(REMOVE_ITEM gui_src "${CMAKE_CURRENT_SOURCE_DIR}/hot_reload.cpp")

    file(GLOB gui_hot_reload_src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
    list(REMOVE_ITEM gui_hot_reload_src "${CMAKE_CURRENT_SOURCE_DIR}/hot_reload.cpp")

    file(GLOB imgui_runtime_src CONFIGURE_DEPENDS "../../.external/imgui/*.cpp" "../../.external/imgui/backends/*.cpp")

    add_library(imgui_runtime SHARED ${imgui_runtime_src})
    set_target_properties(imgui_runtime PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(imgui_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
    target_link_libraries(imgui_runtime PUBLIC ${PHOTON_SDL_TARGET})

    add_library(gui ${gui_src})
    set_target_properties(gui PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
    target_link_libraries(gui PUBLIC ${PHOTON_SDL_TARGET} EnvironmentHeaders EnvironmentAssets imgui_runtime gpu)

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_library(photonUI SHARED hot_reload.cpp ${gui_hot_reload_src})
        target_include_directories(photonUI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
        target_link_libraries(photonUI PRIVATE ${PHOTON_SDL_TARGET} EnvironmentHeaders EnvironmentAssets imgui_runtime gpu)
    endif()
else()
    file(GLOB src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "../../.external/imgui/*.cpp" "../../.external/imgui/backends/*.cpp")
    list(REMOVE_ITEM src "${CMAKE_CURRENT_SOURCE_DIR}/hot_reload.cpp")
    add_library(gui ${src})
    set_target_properties(gui PROPERTIES POSITION_INDEPENDENT_CODE ON)

    file(GLOB gui_hot_reload_src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
    list(REMOVE_ITEM gui_hot_reload_src "${CMAKE_CURRENT_SOURCE_DIR}/hot_reload.cpp")

    target_include_directories(gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
    target_link_libraries(gui PUBLIC ${PHOTON_SDL_TARGET} EnvironmentHeaders EnvironmentAssets gpu)

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(gui PUBLIC v4l2)
    endif()

    if ((CMAKE_SYSTEM_NAME STREQUAL "Linux") AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_library(photonUI SHARED hot_reload.cpp ${gui_hot_reload_src})
        target_include_directories(photonUI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
        target_link_libraries(photonUI PRIVATE gpu)
        target_link_options(photonUI PRIVATE -Wl,-Bsymbolic)
    endif()
endif()
