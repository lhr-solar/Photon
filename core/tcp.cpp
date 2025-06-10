#include "tcp.hpp"
#include <cstdint>
#include <sys/socket.h>
#include <system_error>
#include <iostream>
#include <unistd.h>

TcpSocket::TcpSocket(const std::string& serverIP, unsigned port){
   if(serverIP.size() != 0){
       // client side
    _fd = socket(AF_INET, SOCK_STREAM, 0);
    if(_fd < 0)
        throw std::system_error(errno, std::system_category(), "socket creation failed");


    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);


    if(inet_pton(AF_INET, serverIP.c_str(), &serv_addr.sin_addr) <= 0)
        throw std::system_error(errno, std::system_category(), "pton failed");


    while(connect(_fd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
        std::cout << "[!] Unable to connect to server!" << std::endl;
        sleep(1);
    }

    std::cout << "[+] Initialized connection on: " << _fd << std::endl;

    } else {
        // server side
    _listen = socket(AF_INET, SOCK_STREAM, 0);
    if(_listen < 0)
        throw std::system_error(errno, std::system_category(), "socket creation failed");
    int opt = 1;
    setsockopt(_listen, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT , &opt, sizeof(opt));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if(bind(_listen, (sockaddr*)&addr, sizeof(addr)) < 0)
        throw std::system_error(errno, std::system_category(), "bind failed");
    listen(_listen, 16);
    while( _fd < 0){
        std::cout << "[!] Searching for connection on: " << _listen << std::endl;
        _fd = accept(_listen, nullptr, nullptr);
        sleep(1);
    }
    std::cout << "[+] Initialized connection on: " << _listen << std::endl;
    }
}

TcpSocket::~TcpSocket(){
    if(_fd >= 0) ::close(_fd);
}

ssize_t TcpSocket::read(uint8_t* buf, std::size_t maxlen){
    ssize_t n = ::recv(_fd, buf, maxlen, 0);
    return n;
}

ssize_t TcpSocket::write(const uint8_t* buf, std::size_t len){
    ssize_t n = ::send(_fd, buf, len, MSG_NOSIGNAL);
    return n;
}

void TcpSocket::reconnect(){
    if(_listen < 0)
        return;

    if(_fd >= 0){
        close(_fd);
        _fd = -1;
    }

    while(_fd < 0){
        std::cout << "[!] Attempting reconnect on: " << _listen << std::endl;
        _fd = accept(_listen, nullptr, nullptr);
        sleep(1);
    }
        std::cout << "[+] reconnect on: " << _listen << std::endl;

}
