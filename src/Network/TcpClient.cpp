// src/Network/TcpClient.cpp

#include "TcpClient.h"
#include <iostream>

TcpClient::TcpClient(boost::asio::io_context& io_context, const std::string& host, const std::string& port)
    : resolver_(io_context), socket_(io_context), read_buffer_(256), host_(host), port_(port)
{
}

TcpClient::~TcpClient() {
    socket_.close();
}

void TcpClient::start() {
    doConnect();
}

void TcpClient::doConnect() {
    auto endpoints = resolver_.resolve(host_, port_);
    boost::asio::async_connect(socket_, endpoints,
        [this](const boost::system::error_code& error, const boost::asio::ip::tcp::endpoint& /*endpoint*/) {
            if (!error) {
                doRead();
            } else {
                std::cerr << "TCP connection error: " << error.message() << "\n";
            }
        });
}

void TcpClient::doRead() {
    socket_.async_read_some(boost::asio::buffer(read_buffer_),
        [this](const boost::system::error_code& error, size_t bytes_transferred) {
            handleRead(error, bytes_transferred);
        });
}

void TcpClient::handleRead(const boost::system::error_code& error, size_t bytes_transferred) {
    if (!error) {
        std::vector<uint8_t> data(read_buffer_.begin(), read_buffer_.begin() + bytes_transferred);

        // Call the read callback if set
        if (read_callback_) {
            read_callback_(data);
        }

        // Continue reading
        doRead();
    } else {
        std::cerr << "TCP read error: " << error.message() << "\n";
    }
}

void TcpClient::write(const std::vector<uint8_t>& data) {
    boost::asio::async_write(socket_, boost::asio::buffer(data),
        [](const boost::system::error_code& error, size_t /*bytes_transferred*/) {
            if (error) {
                std::cerr << "TCP write error: " << error.message() << "\n";
            }
        });
}

void TcpClient::setReadCallback(std::function<void(const std::vector<uint8_t>&)> callback) {
    read_callback_ = callback;
}
