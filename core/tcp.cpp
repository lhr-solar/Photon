#include "tcp.hpp"
#include <cstdint>
#include <system_error>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include "config.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <Ws2tcpip.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

TcpSocket::TcpSocket(const std::string& serverIP, unsigned port){
#ifdef _WIN32
    static bool wsa_started = false;
    if(!wsa_started){
        WSADATA wsa;
        int err = WSAStartup(MAKEWORD(2,2), &wsa);
        if(err != 0)
            throw std::system_error(err, std::system_category(), "WSAStartup failed");
        wsa_started = true;
    }
#endif

    if(serverIP.size() != 0){
        // client side
        _fd = socket(AF_INET, SOCK_STREAM, 0);
        if(_fd
#ifdef _WIN32
            == INVALID_SOCKET
#else
            < 0
#endif
        )
#ifdef _WIN32
            throw std::system_error(WSAGetLastError(), std::system_category(), "socket creation failed");
#else
            throw std::system_error(errno, std::system_category(), "socket creation failed");
#endif

#ifdef _WIN32
        //u_long int imode = 1;
        //ioctlsocket(_fd, FIONBIO, &imode);
#endif


        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);

        if(inet_pton(AF_INET, serverIP.c_str(), &serv_addr.sin_addr) <= 0)
#ifdef _WIN32
            throw std::system_error(WSAGetLastError(), std::system_category(), "pton failed");
#else
            throw std::system_error(errno, std::system_category(), "pton failed");
#endif

        while(connect(_fd, (sockaddr*)&serv_addr, sizeof(serv_addr))
#ifdef _WIN32
              == SOCKET_ERROR
#else
              < 0
#endif
        ){
            std::cout << "[!] Unable to connect to server!" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        send(_fd, "xmN10e", 6, 0);
        // send version string
		send(_fd, VERSION, sizeof(VERSION), 0);
        // check if there is an update available
		char response[5] = { 0 };
	    int did_rec = recv(_fd, response, sizeof(response), 0);
        // print to console, received version
        //OutputDebugStringA(response);
        
        // truncate response to only 5 characters
        if (did_rec > 0) {
            if (strncmp(response, VERSION, 5) != 0)
                _update = true;
        }

    } else {
        // server side
        _listen = socket(AF_INET, SOCK_STREAM, 0);
        if(_listen
#ifdef _WIN32
            == INVALID_SOCKET
#else
            < 0
#endif
        )
#ifdef _WIN32
            throw std::system_error(WSAGetLastError(), std::system_category(), "socket creation failed");
#else
            throw std::system_error(errno, std::system_category(), "socket creation failed");
#endif
        int opt = 1;
#ifdef _WIN32
        setsockopt(_listen, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
        setsockopt(_listen, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT , &opt, sizeof(opt));
#endif
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
#ifdef _WIN32
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
#else
        addr.sin_addr.s_addr = INADDR_ANY;
#endif
        addr.sin_port = htons(port);
        if(bind(_listen, (sockaddr*)&addr, sizeof(addr))
#ifdef _WIN32
            == SOCKET_ERROR
#else
            < 0
#endif
        )
#ifdef _WIN32
            throw std::system_error(WSAGetLastError(), std::system_category(), "bind failed");
#else
            throw std::system_error(errno, std::system_category(), "bind failed");
#endif
        listen(_listen, 16);
        while(
#ifdef _WIN32
            _fd == INVALID_SOCKET
#else
            _fd < 0
#endif
        ){
            std::cout << "[!] Searching for connection on: " << _listen << std::endl;
            _fd = accept(_listen, nullptr, nullptr);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::cout << "[+] Initialized connection on: " << _listen << std::endl;
    }
}

TcpSocket::~TcpSocket(){
#ifdef _WIN32
    if(_fd != INVALID_SOCKET) closesocket(_fd);
#else
    if(_fd >= 0) ::close(_fd);
#endif
}

ssize_t TcpSocket::read(uint8_t* buf, std::size_t maxlen) {
#ifdef _WIN32
    int n = ::recv(_fd, reinterpret_cast<char*>(buf), static_cast<int>(maxlen), 0);
    return (n == SOCKET_ERROR) ? -1 : n;
#else
    ssize_t n = ::recv(_fd, buf, maxlen, 0);
    return n;
#endif
}

ssize_t TcpSocket::write(const uint8_t* buf, std::size_t len){
#ifdef _WIN32
    int n = ::send(_fd, reinterpret_cast<const char*>(buf), static_cast<int>(len), 0);
    return (n == SOCKET_ERROR) ? -1 : n;
#else
    ssize_t n = ::send(_fd, buf, len, MSG_NOSIGNAL);
    return n;
#endif
}
void TcpSocket::reconnect(){

#ifdef _WIN32
    if(_listen == INVALID_SOCKET)
        return;

    if(_fd != INVALID_SOCKET){
        closesocket(_fd);
        _fd = INVALID_SOCKET;
    }

    while(_fd == INVALID_SOCKET){
        std::cout << "[!] Attempting reconnect on: " << _listen << std::endl;
        _fd = accept(_listen, nullptr, nullptr);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "[+] reconnect on: " << _listen << std::endl;
#else
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
#endif
}
