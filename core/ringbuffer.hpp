#pragma once

#include <string>
#include <cstdint>
#include <cstddef>
#include <sys/types.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <iostream>

constexpr size_t BUFFERSIZE = 64 * 1024;
constexpr size_t READ_CHUNK = 4096;
class RingBuffer {
    public:
        RingBuffer();
        void write(const uint8_t* data, size_t len);
        size_t read(uint8_t* out, size_t maxlen);
    private:
        uint8_t buf[BUFFERSIZE];
        size_t head, tail, count;
        std::mutex mtx;
        std::condition_variable not_empty, not_full;
};
