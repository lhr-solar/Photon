/*[ξ] the photon network interface*/
#include "network.hpp"
#include "tcp.hpp"
#include "spsc.hpp"
#include <cstddef>
#include <iomanip>
#include <thread>
#include <vector>
#include <iostream>
#include <sstream>
#include "../engine/include.hpp"

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

#define QUEUE_CAPACITY 2048
Network::Network() : spscQueue(QUEUE_CAPACITY) {}

#define BUFFER_CAPACITY 1024
void Network::producer() {
    TcpSocket socket(IP, PORT);
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);

    while (1) {
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

void Network::parser() {
    std::string frame;
    frame.reserve(256);
    bool collecting = false;
    std::string lookahead;  // rolling buffer for pattern detection
    lookahead.reserve(4);

    while (1) {
        if (auto* byte = spscQueue.front()) {
            char ch = static_cast<char>(*byte);
            spscQueue.pop();

            // detect start of standard CAN frame
            if (ch == 't') {
                frame.clear();
                frame.push_back(ch);
                collecting = true;
                continue;
            }

            //  skip until collection begins
            if (!collecting && ch != 'B' && ch != 'S') {
                continue;
            }

            // handle end of a line
            if (ch == '\r') {
                if (!frame.empty()) {
                    // decide what kind of frame this is
                    if (frame.find("BO_") != std::string::npos || frame.find("SG_") != std::string::npos)
                        handleDBCframe(frame);
                    else
                        handleFrame(frame);
                }
                frame.clear();
                collecting = false;
                lookahead.clear();
                continue;
            }

            if (ch == '\n') {
                continue;
            }

            frame.push_back(ch);
            lookahead.push_back(ch);
            if (lookahead.size() > 3) lookahead.erase(0, lookahead.size() - 3);

            // detect new DBC message (BO_)
            if (lookahead == "BO_") {
                logs("[parser] Detected start of DBC message (BO_)");
                frame.clear();
                frame.append("BO_");
                collecting = true;
                continue;
            }

            // detect new DBC signal (SG_)
            if (lookahead == "SG_") {
                logs("[parser] Detected start of DBC signal (SG_)");

                // flush previous BO_ message before collecting this new signal
                if (!frame.empty() && frame.find("BO_") != std::string::npos) {
                    handleDBCframe(frame);
                }

                frame.clear();
                frame.append("SG_");
                collecting = true;
                continue;
            }

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

Network::DbcMessage& Network::ensureDBC(uint32_t canId) {
    std::lock_guard<std::mutex> guard(dbcMapMutex);
    auto it = dbcMap.find(canId);
    if (it == dbcMap.end()) {
        auto inserted = dbcMap.emplace(canId, std::make_unique<DbcMessage>());
        it = inserted.first;
    }
    return *(it->second);
}

void Network::writeSample(uint16_t canId, uint64_t value) {
    sample& entry = ensureSample(canId);
    std::lock_guard<std::mutex> valueGuard(entry.lock);
    entry.point = value;
}

void Network::writeDBC(uint32_t canId, const std::string& name, uint8_t dlc, const std::string& sender) {
    DbcMessage& msg = ensureDBC(canId);
    std::lock_guard<std::mutex> guard(dbcMapMutex);
    msg.name = name;
    msg.dlc = dlc;
    msg.sender = sender;
}

void Network::writeSignal(uint32_t canId, const DbcSignal& sig) {
    DbcMessage& msg = ensureDBC(canId);
    std::lock_guard<std::mutex> guard(dbcMapMutex);
    msg.signals.push_back(sig);
}

bool Network::readSample(uint16_t canId, uint64_t& outValue) {
    std::unique_lock<std::mutex> mapLock(sampleMapMutex);
    auto it = sampleMap.find(canId);
    if (it == sampleMap.end()) return false;
    sample* entry = it->second.get();
    mapLock.unlock();
    std::lock_guard<std::mutex> valueGuard(entry->lock);
    outValue = entry->point;
    return true;
}

//FRAME HANDLING

void Network::handleFrame(const std::string& frame) {
    uint16_t canId = 0;
    uint64_t value = 0;
    if (!decodeFrame(frame, canId, value)) {
        logs("[parser] Invalid SLCAN frame encountered");
        return;
    }
    writeSample(canId, value);
}

//DBC HANDLING

void Network::handleDBCframe(const std::string& frame) {
    if (frame.rfind("BO_", 0) == 0) {
        uint32_t canID = 0; std::string name, sender; uint8_t dlc = 0;
        if (!decodeDBCFrame(frame, canID, name, dlc, sender)) {
            logs("[parser] Invalid DBC BO_ frame encountered");
            return;
        }
        writeDBC(canID, name, dlc, sender);
        currentCanId = canID; // Remember for SG_ lines
        return;
    }
    if (frame.rfind("SG_", 0) == 0) {
        if (currentCanId == 0) {
            logs("[parser] SG_ encountered before BO_");
            return;
        }
        DbcSignal sig;
        if (!decodeSIGFrame(frame, currentCanId, sig)) {
            logs("[parser] Invalid DBC SG_ frame encountered");
            return;
        }
        writeSignal(currentCanId, sig);
        return;
    }
}

bool Network::decodeFrame(const std::string& frame, uint16_t& canId, uint64_t& value) {
    if (frame.empty() || frame.front() != 't') return false;
    if (frame.back() != '\r') return false;
    if (frame.size() < 5) return false;

    int dataLength = hexValue(frame[4]);
    if (dataLength < 0 || dataLength > 8) return false;

    const std::size_t expectedLength = 5 + static_cast<std::size_t>(dataLength) * 2;
    if (frame.size() - 1 != expectedLength) return false;

    canId = 0;
    for (std::size_t i = 1; i <= 3; ++i) {
        int nibble = hexValue(frame[i]);
        if (nibble < 0) return false;
        canId = static_cast<uint16_t>((canId << 4) | static_cast<uint16_t>(nibble));
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

bool Network::decodeDBCFrame(const std::string& frame,
                             uint32_t& canId,
                             std::string& name,
                             uint8_t& dlc,
                             std::string& sender) {
    if (frame.empty() || frame.rfind("BO_", 0) != 0) return false;
    std::istringstream iss(frame);
    std::string tag;
    iss >> tag; // BO_
    iss >> canId;
    std::string temp;
    iss >> temp; // name:
    auto colon = temp.find(':');
    if (colon == std::string::npos) return false;
    name = temp.substr(0, colon);
    std::string dlcStr;
    iss >> dlcStr;
    dlc = static_cast<uint8_t>(std::stoi(dlcStr));
    iss >> sender;
    return true;
}

bool Network::decodeSIGFrame(const std::string& frame,
                             uint32_t& canId,
                             DbcSignal& sig) {
    if (frame.empty() || frame.rfind("SG_", 0) != 0) return false;

    std::istringstream iss(frame);
    std::string tag; iss >> tag;        // SG_
    iss >> sig.name;                    // EngineSpeed
    char colon; iss >> colon;           // :
    iss >> sig.startBit;                // 24
    iss.ignore(1, '|');
    iss >> sig.length;                  // 16
    char at; iss >> at;                 // @
    iss >> sig.byteOrder;               // 1
    char sign; iss >> sign;             // +
    sig.isSigned = (sign == '-');
    return true;
}
