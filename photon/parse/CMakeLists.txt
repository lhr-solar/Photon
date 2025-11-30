file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(parse ${src})

target_include_directories(parse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Vulkan REQUIRED)
target_link_libraries(parse PUBLIC Vulkan::Vulkan KernelHeaders)
add_dependencies(parse GenerateKernelHeaders)

