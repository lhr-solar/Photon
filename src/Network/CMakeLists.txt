#src/Network/CMakeLists.txt

add_library(network_lib
    NetworkManager.cpp
    SerialPort.cpp
    TcpClient.cpp
)

target_include_directories(network_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(network_lib PUBLIC external_asio)
