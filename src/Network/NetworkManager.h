// src/Network/NetworkManager.h

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "SerialPort.h"
#include "TcpClient.h"
#include <boost/asio.hpp>
#include <thread>

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    void start();
    void stop();

    void setSerialReadCallback(std::function<void(const std::vector<uint8_t>&)> callback);
    void setTcpReadCallback(std::function<void(const std::vector<uint8_t>&)> callback);

private:
    void runIoContext();

    boost::asio::io_context io_context_;
    std::unique_ptr<SerialPort> serial_port_;
    std::unique_ptr<TcpClient> tcp_client_;
    std::thread io_thread_;
};

#endif // NETWORK_MANAGER_H
