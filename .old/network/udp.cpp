#include "udp.hpp"
#include "../parse/corsa.hpp"
#ifndef _WIN32
#include <sys/socket.h>
#include <fcntl.h>
#else
#include <mstcpip.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#endif
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>
#include <system_error>

namespace {
bool set_non_blocking(
#ifdef _WIN32
    SOCKET sock
#else
    int sock
#endif
) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) { return false; }
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}
}

UdpSocket::UdpSocket(const std::string& IP, unsigned port){
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

    sock = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    if(sock == INVALID_SOCKET){
        throw std::system_error(WSAGetLastError(), std::system_category(), "socket creation failed");
    }

    BOOL disable_connreset = FALSE;
    DWORD bytes_returned = 0;
    WSAIoctl(sock, SIO_UDP_CONNRESET, &disable_connreset, sizeof(disable_connreset),
             nullptr, 0, &bytes_returned, nullptr, nullptr);
#else
    if(sock == INVALID_SOCKET){
        throw std::system_error(errno, std::system_category(), "socket creation failed");
    }
#endif

    if (!set_non_blocking(sock))
#ifdef _WIN32
        throw std::system_error(WSAGetLastError(), std::system_category(), "failed to set UDP socket non-blocking");
#else
        throw std::system_error(errno, std::system_category(), "failed to set UDP socket non-blocking");
#endif

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_pton(AF_INET, IP.c_str(), &server.sin_addr);
    slen = sizeof(server);
    nextHandshakeTry = std::chrono::steady_clock::now();
}

UdpSocket::~UdpSocket() {
#ifdef _WIN32
    if (sock != INVALID_SOCKET) { closesocket(sock); }
#else
    if (sock != INVALID_SOCKET) { close(sock); }
#endif
}

bool UdpSocket::performHandshake() {
    if (subscribed) { return true; }
    auto now = std::chrono::steady_clock::now();
    if (now < nextHandshakeTry) { return false; }
    nextHandshakeTry = now + std::chrono::milliseconds(300);

    auto send_control_message = [&](operationId op)->bool{
        handshake msg{};
        msg.id = eIPhoneDevice;
        msg.ver = 1;
        msg.opId = op;
        return sendto(sock, reinterpret_cast<const char *>(&msg), sizeof(msg), 0,
                      reinterpret_cast<const sockaddr *>(&server), sizeof(server)) != SOCKET_ERROR;
    };

    if (!send_control_message(HANDSHAKE)) {
#ifdef _WIN32
        std::cerr << "Handshake send failed (WSA error " << WSAGetLastError() << ")\n";
#endif
        return false;
    }

    std::array<char, 2048> handshake_buf{};
    handshakeResponse response{};
    ssize_t recv_len = recvfrom(sock, handshake_buf.data(), static_cast<int>(handshake_buf.size()), 0,
                                reinterpret_cast<sockaddr *>(&server), &slen);
    if (recv_len == SOCKET_ERROR) {
#ifdef _WIN32
        const int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) { return false; }
        std::cerr << "Handshake response failed (WSA error " << err << ")\n";
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) { return false; }
        std::cerr << "Handshake response failed\n";
#endif
        return false;
    }

    if (recv_len < static_cast<ssize_t>(sizeof(handshakeResponse))) {
        std::cerr << "Handshake response truncated (" << recv_len << " bytes)\n";
        return false;
    }

    std::memcpy(&response, handshake_buf.data(), sizeof(response));

    if (!send_control_message(SUBSCRIBE_UPDATE)) {
#ifdef _WIN32
        std::cerr << "Subscribe send failed (WSA error " << WSAGetLastError() << ")\n";
#else
        std::cerr << "Subscribe send failed\n";
#endif
        return false;
    }

    subscribed = true;
    return true;
}

ssize_t UdpSocket::read(RTCarInfo* buf, std::size_t maxlen){
    (void)maxlen;
    if (!subscribed && !performHandshake()) { return 0; }
    ssize_t n = recvfrom(sock, reinterpret_cast<char *>(buf), sizeof(RTCarInfo), 0, reinterpret_cast<sockaddr *>(&server), &slen);
    if (n == SOCKET_ERROR) {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) { return 0; }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) { return 0; }
#endif
        return -1;
    }
    return n;
}

ssize_t UdpSocket::write(const uint8_t* buf, std::size_t len){
    ssize_t size = 0;

    return size;
}

void UdpSocket::reconnect(){

}
