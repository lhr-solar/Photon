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
#include <vector>
#include <iostream>
#include "../engine/include.hpp"

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
};

#define BUFFER_CAPACITY 1024
void Network::tcpReader(){
    TcpSocket socket(IP, PORT);
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);
    while(running){
        auto bytesRead = socket.read(buffer.data(), buffer.size());
        if (bytesRead <= 0) { continue; }
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
    UdpSocket socket(LOCAL_IP, CORSA_PORT);
    std::vector<RTCarInfo> buffer(1);
    while(running){
        auto bytesRead = socket.read(buffer.data(), buffer.size());
        if(bytesRead <= 0){ continue; }
        if(bytesRead > 0){
            std::size_t count = static_cast<std::size_t>(bytesRead) / sizeof(RTCarInfo);
            for(std::size_t i = 0; i < count; i++){
                while(!corsaQueue.try_push(buffer[i])){ std::cout << "[!] Network Buffer full!" << std::endl; std::this_thread::yield();}
            }
        }
    }
};

#ifndef WIN32
#include <termios.h>
void Network::serialReader(){
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);
    int _fd = open("/dev/ttyACM0", O_RDWR | O_NOCTTY);
    while(_fd < 0){
        std::cout << "[!] Attempting connection on /dev/ttyACM0" << std::endl;
        _fd = open("/dev/ttyACM0", O_RDWR | O_NOCTTY);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    struct termios tty = {};
    tcgetattr(_fd, &tty);
    cfmakeraw(&tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;
    tcsetattr(_fd, TCSANOW, &tty);
    tcflush(_fd, TCIFLUSH);

    while(running){
        ssize_t n = read(_fd, buffer.data(), buffer.size());
        for(int i = 0; i < n; i++){
            serialQueue.push(buffer[i]);
        }
    }
};

#else
void Network::serialReader(){

};
#endif
