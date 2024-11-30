// src/Network/NetworkManager.cpp

#include "NetworkManager.h"

NetworkManager::NetworkManager() {
    // Initialize SerialPort and TcpClient
    serial_port_ = std::make_unique<SerialPort>(io_context_, "/dev/ttyUSB0", 9600); // Adjust port and baud rate
    tcp_client_ = std::make_unique<TcpClient>(io_context_, "example.com", "12345"); // Adjust host and port
}

NetworkManager::~NetworkManager() {
    stop();
}

void NetworkManager::start() {
    serial_port_->startRead();
    tcp_client_->start();

    io_thread_ = std::thread([this]() { runIoContext(); });
}

void NetworkManager::stop() {
    io_context_.stop();
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
}

void NetworkManager::runIoContext() {
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard(io_context_.get_executor());
    io_context_.run();
}

void NetworkManager::setSerialReadCallback(std::function<void(const std::vector<uint8_t>&)> callback) {
    serial_port_->setReadCallback(callback);
}

void NetworkManager::setTcpReadCallback(std::function<void(const std::vector<uint8_t>&)> callback) {
    tcp_client_->setReadCallback(callback);
}
