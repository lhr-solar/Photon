file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(parse ${src})

target_include_directories(parse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

#target_link_libraries(this PUBLIC that)
