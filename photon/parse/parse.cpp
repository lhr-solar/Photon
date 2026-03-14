/*[λ] the photon parsing interface*/
#include "parse.hpp"
#include "corsa.hpp"
#include <cstring>
#include <iomanip>
#include <ios>
#include <iostream>
#include <sstream>
#include <string>
#include <variant>

int hexValue(char c) {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return 10 + (c - 'a'); }
    if (c >= 'A' && c <= 'F') { return 10 + (c - 'A'); }
    return -1;
}

std::string toHex(uint64_t i, size_t w){
    std::ostringstream oss;
    oss << std::hex 
        << std::nouppercase 
        << std::setw(w) 
        << std::setfill('0') 
        << i;
    return oss.str();
}

std::string toHex(uint32_t i, size_t w){
    std::ostringstream oss;
    oss << std::hex 
        << std::nouppercase 
        << std::setw(w) 
        << std::setfill('0') 
        << i;
    return oss.str();
}

std::string toHex(uint16_t i, size_t w){
    std::ostringstream oss;
    oss << std::hex 
        << std::nouppercase 
        << std::setw(w) 
        << std::setfill('0') 
        << i;
    return oss.str();
}

std::string toHex(uint8_t i, size_t w){
    std::ostringstream oss;
    oss << std::hex 
        << std::nouppercase 
        << std::setw(w) 
        << std::setfill('0') 
        << static_cast<unsigned int>(i);
    return oss.str();
}

void Parse::writeSample(uint16_t canId, std::array<uint8_t,8>value) {
    if(canId < 0) return;
    sample& entry = ensureSample(canId);
    std::lock_guard<std::mutex> valueGuard(entry.lock);
    entry.point = value;
    entry.hasNew = true;
}

sample& Parse::ensureSample(uint16_t canId) {
    std::lock_guard<std::mutex> guard(sampleMapMutex);
    auto it = sampleMap.find(canId);
    if (it == sampleMap.end()) {
        auto inserted = sampleMap.emplace(canId, std::make_unique<sample>());
        it = inserted.first;
    }
    return *(it->second);
}

bool Parse::readSample(uint16_t canId, std::array<uint8_t,8>& outValue) {
    std::unique_lock<std::mutex> mapLock(sampleMapMutex);
    auto it = sampleMap.find(canId);
    if (it == sampleMap.end()) {
        outValue = {};
        return false;
    }
    sample* entry = it->second.get();
    mapLock.unlock();

    std::lock_guard<std::mutex> valueGuard(entry->lock);
    if (!entry->hasNew) {
        outValue = entry->point;
        return false;
    }
    outValue = entry->point;
    entry->hasNew = false;
    return true;
}

void Parse::handleFrame(const std::string& frame){
    static uint64_t err_count = 0;
    uint16_t canId = 0;
    std::array<uint8_t,8> value = {};
    if (!decodeFrame(frame, canId, value)) { std::cout << "invalid frame " << err_count++ << " | " << canId << std::endl; return; }
    if(canId == 2){
        std::string t = frame;
        t.pop_back();
    }
    writeSample(canId, value);
}


bool Parse::decodeFrame(const std::string& frame, uint16_t& canId, std::array<uint8_t, 8>& value) {
    if (frame.empty() || frame.front() != 't') { return false; }
    if (frame.back() != '\r') { return false; }

    if (frame.size() < 5) { return false; }

    const std::size_t payloadLength = frame.size() - 1;
    if (payloadLength < 5) { return false; }

    int dataLength = hexValue(frame[4]);
    if (dataLength < 0 || dataLength > 8) { return false; }

    const std::size_t expectedLength = 5 + static_cast<std::size_t>(dataLength) * 2;
    if (payloadLength != expectedLength) { return false; }

    canId = 0;
    for (std::size_t i = 1; i <= 3; ++i) {
        int nibble = hexValue(frame[i]);
        if (nibble < 0) { return false; }
        canId = static_cast<uint16_t>((canId << 4) | static_cast<uint16_t>(nibble));
    }

    value = {};
    for (int i = 0; i < dataLength; ++i) {
        uint8_t h = hexValue(frame[5 + i*2]);
        uint8_t l = hexValue(frame[6 + i*2]);
        if (h < 0 || l < 0) { return false; }
        int v = (h<<4)+l;
        value[i] = v;
    }
    return true;
}

void Parse::acParser(SPSCQueue<RTCarInfo>& queue){
    while(running){
        if(auto* t = queue.front()){ queue.pop();
            std::byte* base = reinterpret_cast<std::byte*>(t);
            for(size_t i{0u}; i < RTCarInfo_Fields.size(); i++){
                const FieldInfo& field = RTCarInfo_Fields[i];
                if (field.size > 8) { continue; }

                std::string id = toHex(static_cast<uint16_t>(i), 3);
                std::string dlc = toHex(static_cast<uint8_t>(field.size), 1);

                std::string dt;
                dt.reserve(field.size * 2);

                const uint8_t* payload = reinterpret_cast<const uint8_t*>(base + field.offset);
                for (size_t b = 0; b < field.size; ++b) dt += toHex(payload[field.size - b - 1], 2);
                // least significant -> most significant
                // 0x123 4 AA BB CC DD
                // ∴ little endian
                if(i == 2){
                    float x = *reinterpret_cast<const float*>(base + field.offset);
                    //std::cout << std::endl;
                    //std::cout << x << " : " << dt;
                }
                std::string frame;
                frame.reserve(1 + id.size() + dlc.size() + dt.size() + 1);
                frame += 't';
                frame += id;
                frame += dlc;
                frame += dt;
                frame += '\r';
                handleFrame(frame);
            }
        } else { std::this_thread::yield(); }
    }
}

void Parse::parser(SPSCQueue<uint8_t>& queue){
    std::string frame;
    frame.reserve(32);
    bool collecting = false;

    while(running){
        if(auto* byte = queue.front()){
            char ch = static_cast<char>(*byte);
            queue.pop();

            if (ch == 't'){
                frame.clear();
                frame.push_back(ch);
                collecting = true;
                continue;
            }

            if (!collecting) { continue; }

            if (ch == '\r'){
                frame.push_back(ch);
                handleFrame(frame);
                frame.clear();
                collecting = false;
                continue;
            }

            if (ch == '\n') { continue; }
            frame.push_back(ch);

        }
    }
};

/*
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
        if(unsigned char* l = serialQueue.front()){
            serialQueue.pop();
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

*/
