#include "network.hpp"
#include "tcp.hpp"
#include "dbc_loader.hpp"
#include "dbc_connector.hpp"
#include <thread>
#include <iostream>
#include <sstream>
#include <cctype>

#define QUEUE_CAPACITY 2048
#define BUFFER_CAPACITY 1024

static int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

Network::Network() : spscQueue(QUEUE_CAPACITY) {}

void Network::producer() {
    std::cerr << "[DEBUG] Using IP=" << IP << " PORT=" << PORT << "\n";
    TcpSocket socket(IP, PORT);
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);

    std::cerr << "[+] Attempting connection to " << IP << ":" << PORT << "...\n";
    std::cerr << "[+] Connected (TcpSocket constructor succeeded).\n";


    while (true) {
        auto bytesRead = socket.read(buffer.data(), buffer.size());
        if (bytesRead > 0) {
            for (size_t i = 0; i < (size_t)bytesRead; ++i)
                while (!spscQueue.try_push(buffer[i])) std::this_thread::yield();
        } else std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Network::parser() {
    std::string frame;
    frame.reserve(256);
    bool collecting = false;

    while (true) {
        if (auto* byte = spscQueue.front()) {
            char ch = static_cast<char>(*byte);
            spscQueue.pop();

            if (!collecting && (std::isprint(ch) || ch == '\r'))
                collecting = true;
            if (!collecting) continue;

            if (ch == '\r') {
                if (!frame.empty()) {
                    while (!frame.empty() && (frame.back() == '\r' || frame.back() == '\n'))
                        frame.pop_back();

                    if (frame.rfind("t", 0) == 0) handleFrame(frame);
                }
                frame.clear();
                collecting = false;
                continue;
            }

            frame.push_back(ch);
        } else std::this_thread::yield();
    }
}

bool Network::decodeFrame(const std::string& frame, uint16_t& canId, uint64_t& value) {
    if (frame.empty() || frame.front() != 't' || frame.size() < 5) return false;
    int dataLength = hexValue(frame[4]);
    if (dataLength < 0 || dataLength > 8) return false;

    const size_t expectedLength = 5 + (size_t)dataLength * 2;
    if (frame.size() != expectedLength) return false;

    canId = 0;
    for (size_t i = 1; i <= 3; ++i) {
        int nibble = hexValue(frame[i]);
        if (nibble < 0) return false;
        canId = (uint16_t)((canId << 4) | nibble);
    }

    value = 0;
    for (int i = 0; i < dataLength; ++i) {
        int hi = hexValue(frame[5 + i * 2]);
        int lo = hexValue(frame[6 + i * 2]);
        if (hi < 0 || lo < 0) return false;
        uint8_t byte = (uint8_t)((hi << 4) | lo);
        value = (value << 8) | byte;
    }
    return true;
}

void Network::handleFrame(const std::string& frame) {
    uint16_t canId;
    uint64_t value;
    if (!decodeFrame(frame, canId, value)) {
        std::cerr << "[parser] Invalid SLCAN frame → " << frame << "\n";
        return;
    }

    std::lock_guard<std::mutex> lock(sampleMapMutex);
    auto& entry = sampleMap[canId];
    std::lock_guard<std::mutex> entryLock(entry.lock);
    entry.point = value;

    std::cerr << "[SLCAN] ID=" << canId << " Value=" << std::hex << value << std::dec << "\n";
}

Network::sample& Network::ensureSample(uint16_t canId) {
    std::lock_guard<std::mutex> guard(sampleMapMutex);
    return sampleMap[canId];
}

bool Network::readSample(uint16_t canId, uint64_t& outValue) {
    std::lock_guard<std::mutex> guard(sampleMapMutex);
    auto it = sampleMap.find(canId);
    if (it == sampleMap.end()) return false;
    std::lock_guard<std::mutex> lock(it->second.lock);
    outValue = it->second.point;
    return true;
}

void Network::writeSample(uint16_t canId, uint64_t value) {
    std::lock_guard<std::mutex> guard(sampleMapMutex);
    auto& s = sampleMap[canId];
    std::lock_guard<std::mutex> lock(s.lock);
    s.point = value;
}

bool Network::loadDBC(const std::string& path) {
    std::cerr << "[DBC Loader] Loading DBC from " << path << "\n";
    DbcConnector connector;
    return DbcLoader::loadFromFile(path, connector);
}

void Network::printDBCMap() {
    std::cerr << "[DBC] Dump of loaded messages:\n";
    for (const auto& [id, msg] : dbcMap) {
        std::cerr << "CAN ID: " << id << " Name: " << msg.name
                  << " DLC: " << (int)msg.dlc << " Sender: " << msg.sender << "\n";
    }
}
