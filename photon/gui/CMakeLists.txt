file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "../../.external/imgui/*.cpp")
add_library(gui ${src})

target_include_directories(gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(gui PUBLIC FontHeaders)
add_dependencies(gui GenerateKernelHeaders GenerateFontHeaders)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(gui PUBLIC v4l2)
endif()
