/*[ξ] the photon network interface*/
#include "network.hpp"
#include "tcp.hpp"
#include "spsc.hpp"
#include <cstddef>
#include <iomanip>
#include <thread>
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
Network::Network() : spscQueue(QUEUE_CAPACITY){

}

#define BUFFER_CAPACITY 1024
void Network::producer(){
    TcpSocket socket(IP, PORT);
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);

    while(1){
        auto bytesRead = socket.read(buffer.data(), buffer.size());
        if (bytesRead > 0) {
            for (std::size_t i = 0; i < static_cast<std::size_t>(bytesRead); ++i) {
                while (!spscQueue.try_push(buffer[i])) {
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

    while(1){
        if(auto* byte = spscQueue.front()){
            char ch = static_cast<char>(*byte);
            spscQueue.pop();

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

void Network::writeSample(uint16_t canId, uint64_t value) {
    sample& entry = ensureSample(canId);
    std::lock_guard<std::mutex> valueGuard(entry.lock);
    entry.point = value;
}

bool Network::readSample(uint16_t canId, uint64_t& outValue) {
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
