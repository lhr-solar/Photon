find_program(BASH bash REQUIRED)
find_program(MAKE make REQUIRED)
find_package(Threads REQUIRED)
set(FFMPEG_BUILD ${CMAKE_BINARY_DIR}/ffmpeg)
file(MAKE_DIRECTORY ${FFMPEG_BUILD})
set(FFMPEG_ARCHIVES
    ${FFMPEG_BUILD}/libavcodec/libavcodec.a
    ${FFMPEG_BUILD}/libswscale/libswscale.a
    ${FFMPEG_BUILD}/libavutil/libavutil.a)
add_custom_command(OUTPUT ${FFMPEG_ARCHIVES}
    COMMAND ${BASH} ${CMAKE_SOURCE_DIR}/.external/ffmpeg/configure --disable-everything
        --disable-autodetect --disable-iconv --disable-debug --disable-programs --disable-doc
        --disable-avdevice --disable-avformat --disable-avfilter
        --disable-swresample --disable-network --enable-static --disable-shared --enable-pic
        --enable-decoder=h264 --cc=${CMAKE_C_COMPILER}
    COMMAND ${MAKE} -j${PHOTON_BUILD_JOBS}
    DEPENDS ${CMAKE_SOURCE_DIR}/.external/ffmpeg/configure
            ${CMAKE_SOURCE_DIR}/.external/ffmpeg/Makefile
    WORKING_DIRECTORY ${FFMPEG_BUILD})
add_custom_target(photon_ffmpeg_build DEPENDS ${FFMPEG_ARCHIVES})
add_library(photon_ffmpeg INTERFACE)
add_dependencies(photon_ffmpeg photon_ffmpeg_build)
target_include_directories(photon_ffmpeg INTERFACE ${FFMPEG_BUILD} ${CMAKE_SOURCE_DIR}/.external/ffmpeg)
target_link_libraries(photon_ffmpeg INTERFACE ${FFMPEG_ARCHIVES} Threads::Threads $<$<PLATFORM_ID:Linux>:m>)

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
    photon_ffmpeg
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
        photon_ffmpeg
    )

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(photonUI PRIVATE -Wl,-Bsymbolic)
    endif()
endif()
