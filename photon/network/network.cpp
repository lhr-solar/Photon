/*[ξ] the photon network interface*/
#include "network.hpp"
#include "tcp.hpp"
#include "udp.hpp" 
#include "spsc.hpp"
#include <fcntl.h>
#include <cstddef>
#include <charconv>
#include <map>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>
#include <iostream>
#include <cerrno>
#include <cstdlib>
#include "../engine/include.hpp"

namespace {
#ifndef _WIN32
speed_t toPosixBaud(const std::string& baudRate) {
    const int baud = std::atoi(baudRate.c_str());
    switch (baud) {
        case 600: return B600;
        case 1200: return B1200;
        case 1800:
#ifdef B1800
            return B1800;
#else
            return B1200;
#endif
        case 2400: return B2400;
        case 4800: return B4800;
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        default: return B9600;
    }
}
#else
DWORD toWindowsBaud(const std::string& baudRate) {
    const int baud = std::atoi(baudRate.c_str());
    switch (baud) {
        case 600: return CBR_600;
        case 1200: return CBR_1200;
        case 1800: return 1800;
        case 2400: return CBR_2400;
        case 4800: return CBR_4800;
        case 9600: return CBR_9600;
        case 19200: return CBR_19200;
        case 38400: return CBR_38400;
        default: return CBR_9600;
    }
}
#endif
}

#define QUEUE_CAPACITY 4096
Network::Network() : 
    tcpQueue    (QUEUE_CAPACITY), 
    udpQueue    (QUEUE_CAPACITY), 
    serialQueue (QUEUE_CAPACITY), 
    localQueue  (QUEUE_CAPACITY),
    corsaQueue  (QUEUE_CAPACITY){
    running = true;
}
Network::~Network(){
    running = false;
    currentSource_t.join();
};

#define BUFFER_CAPACITY 1024
void Network::tcpReader(){
    TcpSocket socket(IP, PORT);
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);
    while(running){
        auto bytesRead = socket.read(buffer.data(), buffer.size());
        if (bytesRead <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if (bytesRead > 0) {
            for (std::size_t i = 0; i < static_cast<std::size_t>(bytesRead); i++) {
                while (!tcpQueue.try_push(buffer[i])) { std::cout << "[!] Network Buffer full!" << std::endl; std::this_thread::yield(); }
            }
        }
    }
}

void Network::udpReader(){
};

void Network::localReader(){
};

void Network::corsaReader(){
    std::vector<RTCarInfo> buffer(1);
    constexpr int maxConsecutiveReadFailures = 3;

    while(running){
        UdpSocket socket(LOCAL_IP, CORSA_PORT);
        int consecutiveReadFailures = 0;

        while(running){
            auto bytesRead = socket.read(buffer.data(), buffer.size());
            if(bytesRead == 0){
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            if(bytesRead < 0){
                consecutiveReadFailures++;
                if(consecutiveReadFailures >= maxConsecutiveReadFailures){
                    std::cout << "[!] Corsa UDP read failed repeatedly; restarting reader socket" << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    break;
                }
                continue;
            }

            consecutiveReadFailures = 0;
            std::size_t count = static_cast<std::size_t>(bytesRead) / sizeof(RTCarInfo);
            for(std::size_t i = 0; i < count; i++){
                while(!corsaQueue.try_push(buffer[i])){ std::cout << "[!] Network Buffer full!" << std::endl; std::this_thread::yield();}
            }
        }
    }
};

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
void Network::serialReader(){
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);
    int _fd = open(serialPort.data(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    while((_fd < 0) && running){
        std::cout << "[!] Attempting connection on " << serialPort << std::endl;
        _fd = open(serialPort.data(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    if(_fd < 0) return;
    struct termios tty = {};
    if(tcgetattr(_fd, &tty) != 0){
        close(_fd);
        return;
    }
    cfmakeraw(&tty);
    speed_t speed = toPosixBaud(baudRate);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;
    tcsetattr(_fd, TCSANOW, &tty);
    tcflush(_fd, TCIFLUSH);

    while(running){
        ssize_t n = read(_fd, buffer.data(), buffer.size());
        if(n < 0){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            break;
        }
        if(n == 0){
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        for(int i = 0; i < n; i++){
            serialQueue.push(buffer[i]);
        }
    }
    close(_fd);
};

#else
void Network::serialReader(){
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);
    auto makeDevicePath = [](const std::string& portName)->std::string{
        if(portName.rfind("\\\\.\\", 0) == 0) return portName;
        return std::string("\\\\.\\") + portName;
    };

    HANDLE portHandle = INVALID_HANDLE_VALUE;
    while(running && portHandle == INVALID_HANDLE_VALUE){
        const std::string portPath = makeDevicePath(serialPort);
        portHandle = CreateFileA(
            portPath.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if(portHandle == INVALID_HANDLE_VALUE){
            std::cout << "[!] Attempting connection on " << serialPort << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    if(portHandle == INVALID_HANDLE_VALUE) return;

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if(!GetCommState(portHandle, &dcb)){
        CloseHandle(portHandle);
        return;
    }
    dcb.BaudRate = toWindowsBaud(baudRate);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if(!SetCommState(portHandle, &dcb)){
        CloseHandle(portHandle);
        return;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    SetCommTimeouts(portHandle, &timeouts);

    PurgeComm(portHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);

    while(running){
        DWORD bytesRead = 0;
        if(!ReadFile(portHandle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)){
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if(bytesRead == 0){
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        for(DWORD i = 0; i < bytesRead; ++i){
            serialQueue.push(buffer[static_cast<size_t>(i)]);
        }
    }

    CloseHandle(portHandle);
};
#endif
