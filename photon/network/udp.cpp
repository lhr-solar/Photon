#include "udp.hpp"
#include "../parse/corsa.hpp"
#include <sys/socket.h>
#include <thread>
UdpSocket::UdpSocket(const std::string& IP, unsigned port){
    bool failed = 1;
    while(failed == 1){
    bool f = 0;
    sock = socket(AF_INET, SOCK_DGRAM, 0);

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_pton(AF_INET, IP.c_str(), &server.sin_addr);

    timeval timeout{};
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    handshake hello{};
    hello.id = eIPhoneDevice;
    hello.ver = 1;
    hello.opId = HANDSHAKE;
    if(sendto(sock, reinterpret_cast<const char *>(&hello), sizeof(hello), 0,
               reinterpret_cast<sockaddr *>(&server), sizeof(server)) == SOCKET_ERROR) {
        std::cout << "Failed to send handshake" << std::endl;
        f = 1;
    }

    handshakeResponse response{};
    slen = sizeof(server);
    ssize_t recv_len = recvfrom(sock, reinterpret_cast<char *>(&response), sizeof(response), 0,
    reinterpret_cast<sockaddr *>(&server), &slen);
    if (recv_len < static_cast<ssize_t>(sizeof(response))) {
        std::cerr << "Handshake response failed";
        if (errno == EAGAIN || errno == EWOULDBLOCK) { std::cerr << " (timeout)"; }
        std::cerr << "\n";
        f = 1;
    }

    handshake subscribe{};
    subscribe.id = eIPhoneDevice;
    subscribe.ver = 1;
    subscribe.opId = SUBSCRIBE_UPDATE;
    if (sendto(sock, reinterpret_cast<const char *>(&subscribe), sizeof(subscribe), 0,
               reinterpret_cast<sockaddr *>(&server), sizeof(server)) == SOCKET_ERROR) {
        std::cerr << "Subscribe send failed\n";
        f = 1;
    }

    if(f == 0) failed = 0;
    }
}

ssize_t UdpSocket::read(RTCarInfo* buf, std::size_t maxlen){
    return recvfrom(sock, reinterpret_cast<char *>(buf), sizeof(RTCarInfo), 0, reinterpret_cast<sockaddr *>(&server), &slen);
}

ssize_t UdpSocket::write(const uint8_t* buf, std::size_t len){
    ssize_t size = 0;

    return size;
}

void UdpSocket::reconnect(){

}
