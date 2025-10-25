file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(network ${src})

target_include_directories(network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (WIN32)
    target_link_libraries(network PUBLIC Ws2_32)
endif ()

#target_link_libraries(this PUBLIC that)
