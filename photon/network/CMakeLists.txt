file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(network ${src})

target_include_directories(network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

#target_link_libraries(this PUBLIC that)
