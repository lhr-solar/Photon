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
#include <cstdio>
#include <sstream>
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

std::string toHex(uint16_t value, size_t width) {
    std::string out(width, '0');
    for(size_t i = 0; i < width; ++i){
        const size_t shift = (width - i - 1) * 4;
        const uint8_t nibble = static_cast<uint8_t>((value >> shift) & 0xFu);
        out[i] = static_cast<char>((nibble < 10) ? ('0' + nibble) : ('a' + (nibble - 10)));
    }
    return out;
}

std::string toHex(uint8_t value, size_t width) {
    std::string out(width, '0');
    for(size_t i = 0; i < width; ++i){
        const size_t shift = (width - i - 1) * 4;
        const uint8_t nibble = static_cast<uint8_t>((value >> shift) & 0xFu);
        out[i] = static_cast<char>((nibble < 10) ? ('0' + nibble) : ('a' + (nibble - 10)));
    }
    return out;
}

void pushFrameBytes(SPSCQueue<uint8_t>& queue, const std::string& frame) {
    for(const char ch : frame){
        while(!queue.try_push(static_cast<uint8_t>(ch))){
            logs("[!] Network Buffer full!");
            std::this_thread::yield();
        }
    }
}

#ifndef _WIN32
bool configureCanInterface(const std::string& interfaceName, const std::string& bitRate) {
    const auto runCommand = [](const std::string& command) {
        return std::system(command.c_str()) == 0;
    };

    std::ostringstream downCommand;
    downCommand << "ip link set " << interfaceName << " down >/dev/null 2>&1";
    if(!runCommand(downCommand.str())){
        return false;
    }

    std::ostringstream configCommand;
    configCommand << "ip link set " << interfaceName << " type can bitrate " << bitRate << " >/dev/null 2>&1";
    if(!runCommand(configCommand.str())){
        return false;
    }

    std::ostringstream upCommand;
    upCommand << "ip link set " << interfaceName << " up >/dev/null 2>&1";
    if(!runCommand(upCommand.str())){
        return false;
    }

    return true;
}
#endif
}

#define QUEUE_CAPACITY 4096
Network::Network() : 
    tcpQueue    (QUEUE_CAPACITY), 
    udpQueue    (QUEUE_CAPACITY), 
    serialQueue (QUEUE_CAPACITY), 
    canQueue    (QUEUE_CAPACITY),
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
                while (!tcpQueue.try_push(buffer[i])) { logs("[!] Network Buffer full!"); std::this_thread::yield(); }
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
                    logs("[!] Corsa UDP read failed repeatedly; restarting reader socket");
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    break;
                }
                continue;
            }

            consecutiveReadFailures = 0;
            std::size_t count = static_cast<std::size_t>(bytesRead) / sizeof(RTCarInfo);
            for(std::size_t i = 0; i < count; i++){
                while(!corsaQueue.try_push(buffer[i])){ logs("[!] Network Buffer full!"); std::this_thread::yield();}
            }
        }
    }
};

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
void Network::serialReader(){
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);
    int _fd = open(serialPort.data(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    while((_fd < 0) && running){
        logs("[!] Attempting connection on " << serialPort);
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

void Network::canReader(){
    bool hasLoggedInitialConnection = false;
    bool hasTriedConfigureInterface = false;
    bool hasLoggedConfigureFailure = false;
    while(running){
        if(!hasTriedConfigureInterface){
            if(!configureCanInterface(canInterface, canBitRate)){
                if(!hasLoggedConfigureFailure){
                    logs("[!] Failed to configure CAN interface " << canInterface << " at bitrate " << canBitRate
                         << "; continuing with the interface's existing configuration");
                    hasLoggedConfigureFailure = true;
                }
            } else {
                hasLoggedConfigureFailure = false;
            }
            hasTriedConfigureInterface = true;
        }

        const int socketFd = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
        if(socketFd < 0){
            logs("[!] Failed to open CAN socket on " << canInterface);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        struct ifreq ifr = {};
        std::snprintf(ifr.ifr_name, IFNAMSIZ, "%s", canInterface.c_str());
        if(ioctl(socketFd, SIOCGIFINDEX, &ifr) < 0){
            logs("[!] Attempting connection on " << canInterface);
            close(socketFd);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        sockaddr_can addr = {};
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        if(bind(socketFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0){
            logs("[!] Attempting connection on " << canInterface);
            close(socketFd);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        if(!hasLoggedInitialConnection){
            logs("[+] Connected to CAN interface " << canInterface);
            hasLoggedInitialConnection = true;
        }

        while(running){
            can_frame frame = {};
            const ssize_t bytesRead = read(socketFd, &frame, sizeof(frame));
            if(bytesRead < 0){
                if(errno == EAGAIN || errno == EWOULDBLOCK){
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                break;
            }
            if(bytesRead == 0){
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            if(static_cast<size_t>(bytesRead) < sizeof(can_frame)){ continue; }
            if((frame.can_id & CAN_EFF_FLAG) != 0 || (frame.can_id & CAN_RTR_FLAG) != 0){ continue; }

            std::string serialized;
            serialized.reserve(1 + 3 + 1 + static_cast<size_t>(frame.can_dlc) * 2 + 1);
            serialized += 't';
            serialized += toHex(static_cast<uint16_t>(frame.can_id & CAN_SFF_MASK), 3);
            serialized += toHex(static_cast<uint8_t>(frame.can_dlc & 0xFu), 1);
            for(uint8_t i = 0; i < frame.can_dlc && i < 8; ++i){
                serialized += toHex(frame.data[i], 2);
            }
            serialized += '\r';
            pushFrameBytes(canQueue, serialized);
        }

        close(socketFd);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

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
            logs("[!] Attempting connection on " << serialPort);
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

void Network::canReader(){
    while(running){
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}
#endif
