#pragma once
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace io {

// single-producer single-consumer buffer

//  head_ is written only by the producer
//  tail_ is written only by the consumer
template <typename T, uint32_t N>
class SPSC_Ring {
    static_assert(N != 0 && (N & (N - 1)) == 0,
        "SPSC_Ring: N must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T>,
        "SPSC_Ring: T must be trivially copyable");

    static constexpr uint32_t MASK = N - 1;

public:
    // producers thread

    bool push(const T& item) noexcept {
        const uint32_t h = head_.load(std::memory_order_relaxed);
        // head index
        const uint32_t next = h + 1;
        if (next - tail_.load(std::memory_order_acquire) > N) // max capacity
            return false;   
        buf_[h & MASK] = item;

        head_.store(next, std::memory_order_release);
        return true;
    }

    // consumer thread only
    bool pop(T& out) noexcept {
        const uint32_t t = tail_.load(std::memory_order_relaxed);
        
        if (head_.load(std::memory_order_acquire) == t) // head caught up to tail, no new value
            return false;  
        out = buf_[t & MASK]; // wrap arround (N -1)
        
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    // head - tail index =  size, might be old due to multiple 
    uint32_t size_approx() const noexcept {
        return head_.load(std::memory_order_relaxed)
             - tail_.load(std::memory_order_relaxed);
    }

private:
    // create head and tail pointers in seperate aligned cache lines 
    // so they dont evict each other
    alignas(64) std::atomic<uint32_t> head_{0};
    alignas(64) std::atomic<uint32_t> tail_{0};
    T buf_[N];
};

} // namespace io
