#include "network.hpp"
#include "tcp.hpp"
#include "spsc.hpp"
#include <cstddef>
#include <iomanip>
#include <thread>
#include <vector>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <memory>
#include "../engine/include.hpp"

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

struct DbcSignalInfo {
    int startBit;
    int length;
    int endianness;
    bool isSigned;
    double factor;
    double offset;
    double minVal;
    double maxVal;
};

struct DbcMessageInfo {
    int canId;
    std::string name;
    int dlc;
    std::string transmitter;
    std::unordered_map<std::string, DbcSignalInfo> signals;
};

class DbcConnector {
public:
    std::unordered_map<int, std::shared_ptr<DbcMessageInfo>> dbcMap;

    void registerMessage(int canId, const std::string& name, int dlc, const std::string& transmitter) {
        auto msg = std::make_shared<DbcMessageInfo>();
        msg->canId = canId;
        msg->name = name;
        msg->dlc = dlc;
        msg->transmitter = transmitter;
        dbcMap[canId] = msg;
    }

    void registerSignal(int canId,
                        const std::string& signalName,
                        int startBit,
                        int length,
                        int endianness,
                        bool isSigned,
                        double factor,
                        double offset,
                        double minVal,
                        double maxVal) {
        if (dbcMap.find(canId) == dbcMap.end()) return;
        DbcSignalInfo sig{ startBit, length, endianness, isSigned, factor, offset, minVal, maxVal };
        dbcMap[canId]->signals[signalName] = sig;
    }
};

static DbcConnector dbcConnector;

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
                while (!spscQueue.try_push(buffer[i])) std::this_thread::yield();
            }
        }
    }
}

void Network::parser() {
    std::string frame;
    frame.reserve(256);
    bool collecting = false;
    std::string lookahead;
    lookahead.reserve(4);
    while (1) {
        if (auto* byte = spscQueue.front()) {
            char ch = static_cast<char>(*byte);
            spscQueue.pop();
            if (ch == 't') {
                frame.clear();
                frame.push_back(ch);
                collecting = true;
                continue;
            }
            if (!collecting && ch != 'B' && ch != 'S') continue;
            if (ch == '\r') {
                if (!frame.empty()) {
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
            if (ch == '\n') continue;
            frame.push_back(ch);
            lookahead.push_back(ch);
            if (lookahead.size() > 3) lookahead.erase(0, lookahead.size() - 3);
            if (lookahead == "BO_") {
                logs("[parser] Detected start of DBC message (BO_)");
                frame.clear();
                frame.append("BO_");
                collecting = true;
                continue;
            }
            if (lookahead == "SG_") {
                logs("[parser] Detected start of DBC signal (SG_)");
                if (!frame.empty() && frame.find("BO_") != std::string::npos) handleDBCframe(frame);
                frame.clear();
                frame.append("SG_");
                collecting = true;
                continue;
            }
        } else std::this_thread::yield();
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
    dbcConnector.registerMessage(canId, name, dlc, sender);
}

void Network::writeSignal(uint32_t canId, const DbcSignal& sig) {
    DbcMessage& msg = ensureDBC(canId);
    std::lock_guard<std::mutex> guard(dbcMapMutex);
    msg.signals.push_back(sig);
    dbcConnector.registerSignal(canId, sig.name, sig.startBit, sig.length, sig.byteOrder, sig.isSigned, sig.scale, sig.offset, sig.minVal, sig.maxVal);
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

void Network::handleFrame(const std::string& frame) {
    uint16_t canId = 0;
    uint64_t value = 0;
    if (!decodeFrame(frame, canId, value)) {
        logs("[parser] Invalid SLCAN frame encountered → \"" + frame + "\"");
        return;
    }
    writeSample(canId, value);
}

void Network::handleDBCframe(const std::string& frame) {
    if (frame.rfind("BO_", 0) == 0) {
        uint32_t canID = 0; std::string name, sender; uint8_t dlc = 0;
        if (!decodeDBCFrame(frame, canID, name, dlc, sender)) {
            logs("[parser] Invalid DBC BO_ frame encountered");
            return;
        }
        writeDBC(canID, name, dlc, sender);
        currentCanId = canID;
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

bool Network::decodeDBCFrame(const std::string& frame, uint32_t& canId, std::string& name, uint8_t& dlc, std::string& sender) {
    if (frame.empty() || frame.rfind("BO_", 0) != 0) return false;
    std::istringstream iss(frame);
    std::string tag;
    iss >> tag;
    iss >> canId;
    std::string temp;
    iss >> temp;
    auto colon = temp.find(':');
    if (colon == std::string::npos) return false;
    name = temp.substr(0, colon);
    std::string dlcStr;
    iss >> dlcStr;
    dlc = static_cast<uint8_t>(std::stoi(dlcStr));
    iss >> sender;
    return true;
}

bool Network::decodeSIGFrame(const std::string& frame, uint32_t& canId, DbcSignal& sig) {
    if (frame.empty() || frame.rfind("SG_", 0) != 0) return false;
    std::istringstream iss(frame);
    std::string tag; iss >> tag;
    iss >> sig.name;
    char colon; iss >> colon;
    iss >> sig.startBit;
    iss.ignore(1, '|');
    iss >> sig.length;
    char at; iss >> at;
    iss >> sig.byteOrder;
    char sign; iss >> sign;
    sig.isSigned = (sign == '-');
    return true;
}