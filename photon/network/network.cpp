/*[ξ] the photon network interface*/
#include "network.hpp"
#include "tcp.hpp"
#include "spsc.hpp"
#include <fcntl.h>
#include <cstddef>
#include <charconv>
#include <map>
#include <thread>
#include <termios.h>
#include <vector>
#include <iostream>
#include "../engine/include.hpp"

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

#define QUEUE_CAPACITY 2048
Network::Network() : uartQueue(QUEUE_CAPACITY), tcpQueue(QUEUE_CAPACITY) {
}
Network::~Network(){
    running = false;
};

#define BUFFER_CAPACITY 1024
void Network::producer(){
    TcpSocket socket(IP, PORT);
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);

    while(running){
        auto bytesRead = socket.read(buffer.data(), buffer.size());
        if (bytesRead > 0) {
            for (std::size_t i = 0; i < static_cast<std::size_t>(bytesRead); ++i) {
                while (!tcpQueue.try_push(buffer[i])) {
                    std::this_thread::yield();
                }
            }
        }
    }
}

void Network::parser(){
    std::string frame;
    frame.reserve(32);
    bool collecting = false;

    while(running){
        if(auto* byte = tcpQueue.front()){
            char ch = static_cast<char>(*byte);
            tcpQueue.pop();

            if (ch == 't') {
                frame.clear();
                frame.push_back(ch);
                collecting = true;
                continue;
            }

            if (!collecting) {
                continue;
            }

            if (ch == '\r') {
                frame.push_back(ch);
                handleFrame(frame);
                frame.clear();
                collecting = false;
                continue;
            }

            if (ch == '\n') {
                continue;
            }

            frame.push_back(ch);
        } else {
            std::this_thread::yield();
        }
    }
}

Network::sample& Network::ensureSample(uint16_t canId) {
    std::lock_guard<std::mutex> guard(sampleMapMutex);
    auto it = sampleMap.find(canId);
    if (it == sampleMap.end()) {
        auto inserted = sampleMap.emplace(canId, std::make_unique<sample>());
        it = inserted.first;
    }
    return *(it->second);
}

void Network::writeSample(uint16_t canId, int64_t value) {
    if(canId < 0) return;
    sample& entry = ensureSample(canId);
    std::lock_guard<std::mutex> valueGuard(entry.lock);
    entry.point = value;
}

bool Network::readSample(uint16_t canId, int64_t& outValue) {
    std::unique_lock<std::mutex> mapLock(sampleMapMutex);
    auto it = sampleMap.find(canId);
    if (it == sampleMap.end()) {
        return false;
    }
    sample* entry = it->second.get();
    mapLock.unlock();

    std::lock_guard<std::mutex> valueGuard(entry->lock);
    outValue = entry->point;
    return true;
}

void Network::handleFrame(const std::string& frame) {
    uint16_t canId = 0;
    uint64_t value = 0;
    if (!decodeFrame(frame, canId, value)) {
        logs("[parser] Invalid SLCAN frame encountered");
        return;
    }

    writeSample(canId, value);
    //logs("[parser] CAN 0x" << std::hex << canId << " value 0x" << value << std::dec);
}

bool Network::decodeFrame(const std::string& frame, uint16_t& canId, uint64_t& value) {
    if (frame.empty() || frame.front() != 't') {
        return false;
    }
    if (frame.back() != '\r') {
        return false;
    }

    if (frame.size() < 5) {
        return false;
    }

    const std::size_t payloadLength = frame.size() - 1;
    if (payloadLength < 5) {
        return false;
    }

    int dataLength = hexValue(frame[4]);
    if (dataLength < 0 || dataLength > 8) {
        return false;
    }

    const std::size_t expectedLength = 5 + static_cast<std::size_t>(dataLength) * 2;
    if (payloadLength != expectedLength) {
        return false;
    }

    canId = 0;
    for (std::size_t i = 1; i <= 3; ++i) {
        int nibble = hexValue(frame[i]);
        if (nibble < 0) {
            return false;
        }
        canId = static_cast<uint16_t>((canId << 4) | static_cast<uint16_t>(nibble));
    }

    value = 0;
    for (int i = 0; i < dataLength; ++i) {
        int hi = hexValue(frame[5 + i * 2]);
        int lo = hexValue(frame[6 + i * 2]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        uint8_t byte = static_cast<uint8_t>((hi << 4) | lo);
        value = (value << 8) | byte;
    }

    return true;
}

void Network::uartProducer(){
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
            uartQueue.push(buffer[i]);
        }
    }
};

static inline int retCANID(const char* str){
    static const std::map<std::string, uint16_t> map = {
        {"gX: ", 0x400}, 
        {"gY: ", 0x401},
        {"gZ: ", 0x402},
        {"aX: ", 0x403},
        {"aY: ", 0x404},
        {"aZ: ", 0x405},
        };
    auto it = map.find(str);
    if(it == map.end()) return -1;
    return it->second;
}

void Network::uartConsumer(){
    std::string canId;
    std::string value;
    bool collectingCAN = false;
    bool collectingVal = false;
    while(running){
        if(unsigned char* l = uartQueue.front()){
            uartQueue.pop();
            if((*l == 'g') || (*l == 'a')){
                canId.clear();
                value.clear();
                collectingCAN = true;
                collectingVal = false;
                canId.push_back(*l);
                continue;
            }
            if(collectingCAN && *l == ' '){
                canId.push_back(*l);
                collectingCAN = false;
                collectingVal = true;
                continue;
            }
            if(collectingCAN){
                canId.push_back(*l);
            }
            if(collectingVal && ((*l == ' ') || (*l == '\n'))){
                int v;
                auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), v);
                if (ec == std::errc()) writeSample(retCANID(canId.c_str()), v);
                canId.clear(); value.clear(); collectingVal = false;
            }
            if(collectingVal){
                value.push_back(*l);
            }
        }
    }
}
