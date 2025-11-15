#include <mutex>
#define NOMINMAX
#include <algorithm>
#include "network.hpp"
#include "tcp.hpp"
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

Network::Network()
    : spscQueue(QUEUE_CAPACITY) {}

// ---------------------- producer ----------------------

void Network::producer() {
    std::cerr << "[DEBUG] Using IP=" << IP
              << " PORT=" << PORT << "\n";

    TcpSocket socket(IP, PORT);
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);

    std::cerr << "[+] Attempting connection to "
              << IP << ":" << PORT << "...\n";
    std::cerr << "[+] Connected (TcpSocket constructor succeeded).\n";

    while (true) {
        auto bytesRead = socket.read(buffer.data(), buffer.size());
        if (bytesRead > 0) {
            for (size_t i = 0; i < (size_t)bytesRead; ++i) {
                while (!spscQueue.try_push(buffer[i]))
                    std::this_thread::yield();
            }
        } else {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));
        }
    }
}

// ---------------------- parser ----------------------

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
            if (!collecting)
                continue;

            if (ch == '\r') {
                if (!frame.empty()) {
                    while (!frame.empty() &&
                          (frame.back() == '\r' ||
                           frame.back() == '\n')) {
                        frame.pop_back();
                    }

                    if (!frame.empty() &&
                        frame.rfind("t", 0) == 0) {
                        handleFrame(frame);
                    }
                }
                frame.clear();
                collecting = false;
                continue;
            }

            frame.push_back(ch);
        } else {
            std::this_thread::yield();
        }
    }
}

// ---------------------- decodeFrame ----------------------

bool Network::decodeFrame(const std::string& frame,
                          uint16_t& canId,
                          uint64_t& value) {
    if (frame.empty() || frame.front() != 't'
        || frame.size() < 5)
        return false;

    int dataLength = hexValue(frame[4]);
    if (dataLength < 0 || dataLength > 8)
        return false;

    const size_t expectedLength =
        5 + static_cast<size_t>(dataLength) * 2;
    if (frame.size() != expectedLength)
        return false;

    canId = 0;
    for (size_t i = 1; i <= 3; ++i) {
        int nibble = hexValue(frame[i]);
        if (nibble < 0) return false;
        canId = static_cast<uint16_t>((canId << 4) | nibble);
    }

    value = 0;
    for (int i = 0; i < dataLength; ++i) {
        int hi = hexValue(frame[5 + i * 2]);
        int lo = hexValue(frame[6 + i * 2]);
        if (hi < 0 || lo < 0) return false;
        uint8_t byte = static_cast<uint8_t>((hi << 4) | lo);
        value = (value << 8) | byte;
    }

    return true;
}

// ---------------------- handleFrame ----------------------

void Network::handleFrame(const std::string& frame) {
    uint16_t canId;
    uint64_t value;

    if (!decodeFrame(frame, canId, value)) {
        std::cerr << "[parser] Invalid SLCAN frame → "
                  << frame << "\n";
        return;
    }

    {
        std::lock_guard<std::mutex> guard(sampleMapMutex);
        auto& entry = sampleMap[canId];
        std::lock_guard<std::mutex> entryLock(entry.lock);
        entry.point = value;
    }

    std::cerr << "[SLCAN] ID=" << canId
        << " Value=0x" << std::hex << value
        << std::dec << "\n";

    // Interpret using DBC if available
    if (dbcManager.hasMessages()) {
        interpretDBCFrame(canId, value, frame[4] - '0');  // DLC from frame[4]
    } else {
        std::cerr << "[DBC] Map is empty.\n";
    }

}

// ---------------------- sample helpers ----------------------

Network::sample& Network::ensureSample(uint16_t canId) {
    std::lock_guard<std::mutex> guard(sampleMapMutex);
    return sampleMap[canId];
}

bool Network::readSample(uint16_t canId, uint64_t& outValue) {
    std::lock_guard<std::mutex> guard(sampleMapMutex);
    auto it = sampleMap.find(canId);
    if (it == sampleMap.end())
        return false;

    std::lock_guard<std::mutex> lock(it->second.lock);
    outValue = it->second.point;
    return true;
}

// ---------------------------------------------------------
// Decode raw CAN payload using the DBC map
// ---------------------------------------------------------
void Network::interpretDBCFrame(uint16_t canId, uint64_t rawValue, int dlc) {
    std::lock_guard<std::mutex> lock(dbcManager.mapMutex);

    auto it = dbcManager.dbcMap.find(canId);
    if (it == dbcManager.dbcMap.end()) {
        std::cerr << "[DBC Decode] No DBC entry for CAN ID " << canId << "\n";
        return;
    }

    const DbcMessage& msg = it->second;

    // ---- COLLECT INTO A VECTOR ----
    std::vector<std::string> collected;
    collected.push_back(msg.name);

    // Convert rawValue → bytes
    uint8_t bytes[8] = {0};
    uint64_t tmp = rawValue;
    for (int i = dlc - 1; i >= 0; --i) {
        bytes[i] = static_cast<uint8_t>(tmp & 0xFF);
        tmp >>= 8;
    }

    // Iterate signals
    for (const auto& [sigName, sig] : msg.signals) {

        uint64_t rawSignal = 0;
        int byteIndex = sig.startBit / 8;
        int bitOffset = sig.startBit % 8;
        int bitsLeft = sig.length;
        int dstPos = 0;

        while (bitsLeft > 0 && byteIndex < dlc) {
            int bitsInByte = std::min(8 - bitOffset, bitsLeft);
            uint8_t mask = ((1 << bitsInByte) - 1) << bitOffset;
            uint8_t extracted = (bytes[byteIndex] & mask) >> bitOffset;
            rawSignal |= (uint64_t(extracted) << dstPos);

            bitsLeft -= bitsInByte;
            dstPos += bitsInByte;
            byteIndex++;
            bitOffset = 0;
        }

        double physValue = sig.scale * rawSignal + sig.offset;

        // ---- MAKE A STRING FOR THIS SIGNAL ----
        std::ostringstream ss;
        ss << sigName << " = " << physValue;
        if (!sig.unit.empty())
            ss << " " << sig.unit;

        collected.push_back(ss.str());

        // ---- Print OUT (original behavior) ----
        std::cerr << "    " << ss.str() << "\n";
    }

    // ---- STORE IN HISTORY ----
    {
        std::lock_guard<std::mutex> histLock(decodedHistoryMutex);
        decodedHistory.push_back({canId, rawValue, collected});
        if (decodedHistory.size() > MAX_HISTORY)
            decodedHistory.pop_front();
    }
}

void Network::writeSample(uint16_t canId, uint64_t value) {
    std::lock_guard<std::mutex> guard(sampleMapMutex);
    auto& s = sampleMap[canId];
    std::lock_guard<std::mutex> lock(s.lock);
    s.point = value;
}

// ---------------------- DBC wrappers ----------------------

bool Network::loadDBC(const std::string& path) {
    return dbcManager.loadFromFile(path);
}

void Network::printDBCMap() {
    dbcManager.dump();
}
