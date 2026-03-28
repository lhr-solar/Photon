#include <string>
#include <iostream>
#include <chrono>
#include "../parse/corsa.hpp"

// Windows
#if defined(_WIN32)
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include<winsock2.h>
#include <Ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SSIZE_T ssize_t;

// Linux
#else
#include <sys/socket.h>
#include <arpa/inet.h> // This contains inet_addr
#include <unistd.h> // This contains close
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif


// These could also be enums
const int socket_bind_err = 3;
const int socket_accept_err = 4;
const int connection_err = 5;
const int message_send_err = 6;
const int receive_err = 7;

struct UdpSocket{
    UdpSocket(const std::string& IP, unsigned port);
    ~UdpSocket();
    ssize_t read(RTCarInfo* buf, std::size_t maxlen);
    ssize_t write(const uint8_t* buf, std::size_t len);
    void reconnect();
    bool performHandshake();

#ifdef _WIN32
    SOCKET sock;
#else
    int sock;
#endif
    sockaddr_in server{};
    socklen_t slen{};
    bool subscribed = false;
    std::chrono::steady_clock::time_point nextHandshakeTry{};
};
