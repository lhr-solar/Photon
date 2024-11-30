add_library(Network STATIC
    SerialPort.cpp
    TcpClient.cpp
    NetworkManager.cpp
)

target_include_directories(Network
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include/Network
        ${Boost_INCLUDE_DIRS}
)

target_link_libraries(Network
    PRIVATE
        ${Boost_LIBRARIES}
)


