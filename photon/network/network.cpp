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
                frame.clear(); // clelaing the message frame
                frame.push_back(ch); //appending the ch to the end
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

            if (!collecting && ch == 'B') {
                frame.push_back(ch);
                frame.push_back(ch);
                frame.push_back(ch);
                if (frame.size() >= 3 && frame.substr(frame.size() - 3) == "BO_") {
            logs("[parser] Detected DBC message definition");
            frame.clear();
            frame.push_back(ch);
            collecting = true;
            continue;
        }
    }
            if (collecting && frame.size() >= 3 && frame[0] == 'B') {
                // Detect if we just started a new SG_ inside a DBC message
                size_t len = frame.size();
                if (len >= 3 && frame[len - 3] == 'S' && frame[len - 2] == 'G' && ch == '_') {
                    // Complete the "SG_" sequence
                    frame.push_back(ch);

                    // Handle the previous BO_ frame before starting a new one
                    handleDBCframe(frame);
                    // example: store message ID for current context
                    // Sigid = canID; // if you have a signal–to–message mapping

                    // Start collecting new SG_ line
                    frame.clear();
                    frame.append("SG_");
                    collecting = true;
                    continue;
                }
}

            frame.push_back(ch);
        } else { //doesnt this mean that we'll only get the packets if the queue is empty?
            std::this_thread::yield();
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

Network::sample& Network::ensureDBC(uint16_t canId, uint64_t rest_of_stuff) {
    std::lock_guard<std::mutex> guard(sampleMapMutex);
    auto it = sampleMap.find(canId);
    if (it == sampleMap.end()) {

        auto inserted = (canId)Map.emplace(canId, std::make_unique<sample>());
        it = inserted.first;
    }
    return *(it->second);
}

void Network::writeSample(uint16_t canId, uint64_t value) {
    sample& entry = ensureSample(canId);
    std::lock_guard<std::mutex> valueGuard(entry.lock);
    entry.point = value;
}

void Network::writeDBC(uint16_t canId, uint64_t value) {
    sample& entry = ensureSample(canId);
    std::lock_guard<std::mutex> valueGuard(entry.lock);
    entry.point = value;
}

void Network::writeSignal(uint16_t canId, uint64_t value) {
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

void Network::handleDBCframe(const std::string& frame) {
    if (frame[0] == 'B') {
        uint16t_t canID = 0;
        uint64t_t rest_of_stuff = 0;
        if (!decodeDBCFrame(frame, canID, value)) {
            logs("[parser] Invalid DBC frame encountered");
            return;
        writeDBC(canID, rest_of_stuff);
    }
    else if (frame[0] == 'S') {
        uint16t_t canID = 0;
        uint64t_t signal = 0;
        if (!decodeSIGFrame(frame, canID, value)) {
            logs("[parser] Invalid signal frame encountered");
            return;
        writeSignal(canID, signal);
        }
    }

    
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

bool Network::decodeDBCFrame(const std::string& frame, uint16_t& canId, uint64_t& rest_of__stuff) {
    if (frame.empty() || frame.front() != 'B') {
        return false;
    }

    // Example: frame = "BO_ 1234:5678"
    // Index 1–4 contain CAN ID digits (e.g., 1234)
    size_t index = 1;
    canId = 0;

    // Ensure there are digits at position 1–4
    for (; index < frame.size() && std::isdigit(static_cast<unsigned char>(frame[index])); ++index) {
        canId = canId * 10 + (frame[index] - '0');
    }

    // Expect a colon next
    if (index >= frame.size() || frame[index] != ':') {
        return false;
    }
    ++index; // move past the colon

    // Parse value (up to the end or until a non-digit)
    value = 0;
    for (; index < frame.size(); ++index) {
        char ch = frame[index];
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
        value = value * 10 + (ch - '0');
    }

    return true;
}


bool Network::decodeSIGFrame(const std::string& frame, uint16_t& canId, uint64_t& signal) {
    // minimal format validation, not full parse
    if (frame.empty() || frame.rfind("SG_", 0) != 0) {
        return false;
    }

    // example structure: SG_ SignalName : 24|16@1+ (0.125,0) [0|8000] "rpm" Vector__XXX
    for (size_t i = 0; i < frame.size(); ++i) {
        char ch = frame[i];

        // colon must have space before and after ( ...name : startbit... )
        if (ch == ':') {
            if (i == 0 || i + 1 >= frame.size()) return false;
            char before = frame[i - 1];
            char after  = frame[i + 1];
            if (!std::isspace(static_cast<unsigned char>(before)) ||
                !std::isspace(static_cast<unsigned char>(after))) {
                logs("[decodeSIGFrame] invalid spacing around ':'");
                return false;
            }
        }

        // '|' separates start bit and length => must have digits on both sides
        else if (ch == '|') {
            if (i == 0 || i + 1 >= frame.size()) return false;
            char before = frame[i - 1];
            char after  = frame[i + 1];
            if (!std::isdigit(static_cast<unsigned char>(before)) ||
                !std::isdigit(static_cast<unsigned char>(after))) {
                logs("[decodeSIGFrame] '|' not surrounded by digits");
                return false;
            }
        }

        // '@' should be followed by '0' or '1' (byte order indicator)
        else if (ch == '@') {
            if (i + 1 >= frame.size()) return false;
            char after = frame[i + 1];
            if (after != '0' && after != '1') {
                logs("[decodeSIGFrame] '@' not followed by 0 or 1");
                return false;
            }
        }
    }

    // if we reach here, the line looks structurally correct
    // (you can later expand this to extract startBit/length/etc.)
    canId = 0;   // not encoded in SG_ line itself — usually inherited from BO_
    signal = 0;  // placeholder until real parsing is implemented

    return true;
}

    // new changes

}
