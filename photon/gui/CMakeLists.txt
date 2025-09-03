file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "../../.external/imgui/*.cpp")
add_library(gui ${src})

target_include_directories(gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
add_dependencies(gui GenerateKernelHeaders)

#target_link_libraries(this PUBLIC that)
