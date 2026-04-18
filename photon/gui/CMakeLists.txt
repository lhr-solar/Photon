file(GLOB src
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DDash/*.cpp"
    "../../.external/imgui/*.cpp"
)
add_library(gui ${src})

target_include_directories(gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/DDash
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(gui PUBLIC FontHeaders)
add_dependencies(gui GenerateKernelHeaders GenerateFontHeaders)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_library(TURBOJPEG_LIB turbojpeg REQUIRED)
    find_library(AVCODEC_LIB  avcodec  REQUIRED)
    find_library(AVUTIL_LIB   avutil   REQUIRED)
    find_library(SWSCALE_LIB  swscale  REQUIRED)
    target_link_libraries(gui PUBLIC v4l2
        ${TURBOJPEG_LIB}
        ${AVCODEC_LIB}
        ${AVUTIL_LIB}
        ${SWSCALE_LIB})
endif()
