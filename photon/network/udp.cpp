#include "udp.hpp"
#include "../parse/corsa.hpp"
#ifndef WIN32
#include <sys/socket.h>
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
    bool failed = 1;
    while(failed == 1){
    bool f = 0;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    if(sock == INVALID_SOCKET){
        throw std::system_error(WSAGetLastError(), std::system_category(), "socket creation failed");
    }

    DWORD timeout_ms = 2000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));

    BOOL disable_connreset = FALSE;
    DWORD bytes_returned = 0;
    WSAIoctl(sock, SIO_UDP_CONNRESET, &disable_connreset, sizeof(disable_connreset),
             nullptr, 0, &bytes_returned, nullptr, nullptr);
#endif

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_pton(AF_INET, IP.c_str(), &server.sin_addr);

#ifndef WIN32
    timeval timeout{};
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    auto send_control_message = [&](operationId op)->bool{
        handshake msg{};
        msg.id = eIPhoneDevice;
        msg.ver = 1;
        msg.opId = op;
        return sendto(sock, reinterpret_cast<const char *>(&msg), sizeof(msg), 0,
                      reinterpret_cast<const sockaddr *>(&server), sizeof(server)) != SOCKET_ERROR;
    };

    std::array<char, 2048> handshake_buf{};
    handshakeResponse response{};
    constexpr int kMaxHandshakeAttempts = 5;
    bool handshake_ok = false;
    for (int attempt = 1; attempt <= kMaxHandshakeAttempts && !handshake_ok; ++attempt) {
        if (!send_control_message(HANDSHAKE)) {
#ifdef _WIN32
            std::cerr << "Handshake send failed (WSA error " << WSAGetLastError() << ")\n";
#else
            std::cerr << "Handshake send failed\n";
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            continue;
        }

        handshake_buf.fill(0);
        slen = sizeof(server);
        ssize_t recv_len = recvfrom(sock, handshake_buf.data(), static_cast<int>(handshake_buf.size()), 0,
                                    reinterpret_cast<sockaddr *>(&server), &slen);
        if (recv_len == SOCKET_ERROR) {
#ifdef _WIN32
            const int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                std::cerr << "Handshake timeout (" << attempt << "/" << kMaxHandshakeAttempts << ")\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                continue;
            }
            std::cerr << "Handshake response failed (WSA error " << err << ")\n";
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::cerr << "Handshake timeout (" << attempt << "/" << kMaxHandshakeAttempts << ")\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                continue;
            }
            std::cerr << "Handshake response failed\n";
#endif
            f = 1;
            break;
        }

        if (recv_len < static_cast<ssize_t>(sizeof(handshakeResponse))) {
            std::cerr << "Handshake response truncated (" << recv_len << " bytes)\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            continue;
        }

        std::memcpy(&response, handshake_buf.data(), sizeof(response));
        handshake_ok = true;
    }

    if (!handshake_ok) {
        std::cerr << "No handshake response after " << kMaxHandshakeAttempts << " attempts\n";
        f = 1;
    }

    if (!f) {
        if (!send_control_message(SUBSCRIBE_UPDATE)) {
#ifdef _WIN32
            std::cerr << "Subscribe send failed (WSA error " << WSAGetLastError() << ")\n";
#else
            std::cerr << "Subscribe send failed\n";
#endif
            f = 1;
        }
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
