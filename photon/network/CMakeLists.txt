set(src
    "${CMAKE_CURRENT_SOURCE_DIR}/network.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dbc_manager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tcp.cpp"
)
add_library(network ${src})

target_include_directories(network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (WIN32)
    target_link_libraries(network PUBLIC Ws2_32)
endif ()

#target_link_libraries(this PUBLIC that)
