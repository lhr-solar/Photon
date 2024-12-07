// src/Network/TcpClient.h

#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include <boost/asio.hpp>
#include <functional>
#include <string>
#include <vector>

class TcpClient {
public:
    TcpClient(boost::asio::io_context& io_context, const std::string& host, const std::string& port);
    ~TcpClient();

    void start();
    void write(const std::vector<uint8_t>& data);

    void setReadCallback(std::function<void(const std::vector<uint8_t>&)> callback);

private:
    void doConnect();
    void doRead();
    void handleRead(const boost::system::error_code& error, size_t bytes_transferred);

    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;
    std::vector<uint8_t> read_buffer_;
    std::function<void(const std::vector<uint8_t>&)> read_callback_;
    std::string host_;
    std::string port_;
};

#endif // TCP_CLIENT_H
