file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(parse ${src})

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libswscale libavutil)

target_include_directories(parse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${FFMPEG_INCLUDE_DIRS})

if (FFMPEG_CFLAGS_OTHER)
    target_compile_options(parse PRIVATE ${FFMPEG_CFLAGS_OTHER})
endif()

target_link_libraries(parse PUBLIC ${FFMPEG_LIBRARIES})
