#include "arena.hpp"
#include <memory>
#include <sys/mman.h>

#include <iostream>

void Arena::init(const arenaConfig& config){
    if(config.validIds.empty()) return;
    for(const auto& m : messages) if(m) destroy(m->id);
    arenaSize = MINIMUM_ARENA_SIZE;
    if(config.arenaSize > MINIMUM_ARENA_SIZE) arenaSize = config.arenaSize;
    pool = mmap(nullptr, arenaSize, 
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for(const auto& idx : config.validIds){
        const auto count = config.signalCounts[idx];
        totalSignals += count;
    };
    std::cout << "total signals == " << totalSignals << std::endl;

    uint8_t* cursor = static_cast<uint8_t*>(pool);
    size_t remaining = arenaSize;

    auto alloc = [&](size_t bytes, size_t align) -> void* {
        void* p = cursor;
        if (!std::align(align, bytes, p, remaining)) return nullptr;
        cursor = static_cast<uint8_t*>(p) + bytes;
        remaining -= bytes;
        return p;
    };
    int totalPages = arenaSize / PAGE_SIZE;
    std::cout << totalPages << std::endl;
    int pagesPerSignal = totalPages / totalSignals;
    size_t bytesPerSignal = PAGE_SIZE * pagesPerSignal;
    for(const auto& idx : config.validIds){
        messages[idx] = new(Message);
        Message& msg = *messages[idx];
        msg.id = idx;
        msg.dlc = config.dataLengths[idx];
        msg.signalCount = config.signalCounts[idx];
        for(auto i{0uz}; i < msg.signalCount; i++){
            msg.signals[i] = new(Signal);
            uint32_t size = (PAGE_SIZE*pagesPerSignal)/sizeof(double);
            void* mem = alloc(bytesPerSignal, PAGE_SIZE); // or alignof(double)
            msg.signals[i]->data = mem;
            std::cout << size << " data points" << " ==  " << bytesPerSignal / 1024 << "kb" << std::endl;
        };
    }
}

void Arena::destroy(uint32_t id){ 
    munmap(pool, arenaSize);
    /*
    if(messages[id] != nullptr){
        for(auto i{0uz}; i < messages[id]->signalCount; i++)
            if(messages[id]->signals[i] != nullptr)
                free(messages[id]->signals[i]->data);
        delete(messages[id]);
    } 
    */
}

/*
If you insist on independent allocations, use aligned allocation:

msg.signals[i]->data =
    std::aligned_alloc(PAGE_SIZE, bytesPerSignal);

Constraints:

* size must be multiple of PAGE_SIZE
* still slower and fragmented vs arena

Key issue in your current code:
`malloc(size * sizeof(double))` ignores your page model entirely. You compute pages, but allocator does not care.

Also you are recomputing `pagesPerSignal` inside the loop; it is invariant. Move it out and verify `totalSignals != 0`.

Conclusion:
If alignment matters, stop allocating per signal. Allocate once (mmap/aligned_alloc) and sub-allocate with `std::align`.
*/
