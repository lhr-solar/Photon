file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "../../.external/imgui/*.cpp")
add_library(gui ${src})

target_include_directories(gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)

# Only add kernel header dependency if the target exists
if(TARGET GenerateKernelHeaders)
    add_dependencies(gui GenerateKernelHeaders)
endif()

#target_link_libraries(this PUBLIC that)
