// src/Network/SerialPort.cpp

#include "SerialPort.h"
#include <iostream>

SerialPort::SerialPort(boost::asio::io_context& io_context, const std::string& port_name, unsigned int baud_rate)
    : serial_(io_context, port_name), read_buffer_(256)
{
    serial_.set_option(boost::asio::serial_port_base::baud_rate(baud_rate));
}

SerialPort::~SerialPort() {
    serial_.close();
}

void SerialPort::startRead() {
    doRead();
}

void SerialPort::doRead() {
    serial_.async_read_some(boost::asio::buffer(read_buffer_),
        [this](const boost::system::error_code& error, size_t bytes_transferred) {
            handleRead(error, bytes_transferred);
        });
}

void SerialPort::handleRead(const boost::system::error_code& error, size_t bytes_transferred) {
    if (!error) {
        std::vector<uint8_t> data(read_buffer_.begin(), read_buffer_.begin() + bytes_transferred);

        // Call the read callback if set
        if (read_callback_) {
            read_callback_(data);
        }

        // Continue reading
        doRead();
    } else {
        std::cerr << "Serial port read error: " << error.message() << "\n";
    }
}

void SerialPort::write(const std::vector<uint8_t>& data) {
    boost::asio::async_write(serial_, boost::asio::buffer(data),
        [](const boost::system::error_code& error, size_t /*bytes_transferred*/) {
            if (error) {
                std::cerr << "Serial port write error: " << error.message() << "\n";
            }
        });
}

void SerialPort::setReadCallback(std::function<void(const std::vector<uint8_t>&)> callback) {
    read_callback_ = callback;
}
