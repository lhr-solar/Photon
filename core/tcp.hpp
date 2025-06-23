#pragma once

#include <string>
#include <cstdint>
#include <cstddef>
#include <sys/types.h>

#ifdef _WIN32
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <BaseTsd.h>
#include <Windows.h>
typedef SSIZE_T ssize_t;
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

class TcpSocket {
    public:
        TcpSocket(const std::string& serverIP, unsigned port);
        ~TcpSocket();

        TcpSocket(const TcpSocket&) = delete;
        TcpSocket& operator=(const TcpSocket&) = delete;
        ssize_t read(uint8_t* buf, std::size_t maxlen);
        ssize_t write(const uint8_t* buf, std::size_t len);
        void reconnect();

    private:
#ifdef _WIN32
        SOCKET _fd = INVALID_SOCKET;
        SOCKET _listen = INVALID_SOCKET;
#else
        int _fd = -1;
        int _listen = -1;
#endif
};
