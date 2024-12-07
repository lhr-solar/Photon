// src/Network/SerialPort.h

#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

#include <boost/asio.hpp>
#include <functional>
#include <string>
#include <vector>

class SerialPort {
public:
    SerialPort(boost::asio::io_context& io_context, const std::string& port_name, unsigned int baud_rate);
    ~SerialPort();

    void startRead();
    void write(const std::vector<uint8_t>& data);

    void setReadCallback(std::function<void(const std::vector<uint8_t>&)> callback);

private:
    void doRead();
    void handleRead(const boost::system::error_code& error, size_t bytes_transferred);

    boost::asio::serial_port serial_;
    std::vector<uint8_t> read_buffer_;
    std::function<void(const std::vector<uint8_t>&)> read_callback_;
};

#endif // SERIAL_PORT_H
