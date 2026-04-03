#include "arena.hpp"
#include <cstring>
#include <iomanip>
#include <memory>
#include <sys/mman.h>
#include <iostream>
#include "../engine/include.hpp"

inline std::string fmtb(uint64_t b){
    static constexpr std::array<const char*,6> u{"B","KB","MB","GB","TB","PB"};
    double v = b;
    size_t i = 0;
    while(v >= 1024.0 && i < u.size()-1){ v /= 1024.0; ++i; }
    std::ostringstream s;
    s << std::fixed << std::setprecision(2) << v << ' ' << u[i];
    return s.str();
}

void Arena::status(){
    logs("arena size        : " << fmtb(arenaSize));
    logs("total signals     : " << totalSignals);
    logs("total pages       : " << totalPages);
    logs("bytes per signal  : " << fmtb(bytesPerSignal));
    logs("unused            : " << fmtb((arenaSize - (bytesPerSignal * totalSignals))));
    logs("points per signal : " << bytesPerSignal / sizeof(double));
};

void Arena::init(const arenaConfig& config){
    if(config.validIds.empty()) return;
    for(const auto& m : messages) if(m) clear(m->id);
    arenaSize = MINIMUM_ARENA_SIZE;
    if(config.arenaSize > MINIMUM_ARENA_SIZE) arenaSize = config.arenaSize;
    pool = mmap(nullptr, arenaSize, 
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for(const auto& idx : config.validIds){
        uint32_t count = config.signalCounts[idx];
        if(count > 32) count = 32;
        totalSignals += count;
    };
    cursor = static_cast<uint8_t*>(pool);
    remaining = arenaSize;
    totalPages = arenaSize / PAGE_SIZE;
    pagesPerSignal = totalPages / totalSignals;
    bytesPerSignal = PAGE_SIZE * pagesPerSignal; 

    for(const auto& idx : config.validIds){
        messages[idx] = new(Message);
        Message& msg = *messages[idx];
        msg.id = idx;
        msg.dlc = config.dataLengths[idx];
        msg.signalSize.store(0, std::memory_order_relaxed);
        msg.signalCount = config.signalCounts[idx];
        if(msg.signalCount > 32) msg.signalCount = 32;
        for(auto i{0uz}; i < msg.signalCount; i++){
            msg.signals[i] = new(Signal);
            void* mem = alloc(bytesPerSignal, PAGE_SIZE);
            msg.signals[i]->data = mem;
        };
    }
    status();
}

void* Arena::alloc(size_t bytes, size_t align){
    void* p = cursor;
    if (!std::align(align, bytes, p, remaining)) return nullptr;
    cursor = static_cast<uint8_t*>(p) + bytes;
    remaining -= bytes;
    return p;
};

// clears the existing message
// if no message exists, simply returns
void Arena::clear(uint32_t id){
    if(id >= messages.size() || !messages[id]) return;
    messages[id]->signalSize.store(0, std::memory_order_release);
};

// thread safe read
// returns a pointer of the signals buffer
// returns the current populated size of the buffer 
void Arena::read(uint32_t id, uint32_t signal, void** data, uint32_t* size){
    if(data) *data = nullptr;
    if(size) *size = 0;
    if(id >= messages.size() || !messages[id]) return;

    Message& msg = *messages[id];
    if(signal >= msg.signalCount || !msg.signals[signal]) return;

    const uint32_t published = msg.signalSize.load(std::memory_order_acquire);
    if(data) *data = msg.signals[signal]->data;
    if(size) *size = published;
};

// thread safe write
// appends the data to the signal buffer
bool Arena::write(uint32_t id, uint32_t signal, void* data, uint32_t size){
    if(id >= messages.size() || !messages[id] || !data) return false;
    Message& msg = *messages[id];
    if(signal >= msg.signalCount || !msg.signals[signal]) return false;

    const uint32_t offset = msg.signalSize.load(std::memory_order_relaxed);
    if(offset > bytesPerSignal || size > bytesPerSignal - offset) return false;

    auto* dst = static_cast<uint8_t*>(msg.signals[signal]->data) + offset;
    std::memcpy(dst, data, size);
    msg.signalSize.store(offset + size, std::memory_order_release);
    return true;
};

void Arena::destroy(){ 
    for(const auto& id : validIds) clear(id);
    munmap(pool, arenaSize);
}
