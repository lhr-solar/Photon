file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(gui ${src})

target_include_directories(gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

#target_link_libraries(this PUBLIC that)
