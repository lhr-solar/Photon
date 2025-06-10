#include "ringbuffer.hpp"
#include <cstddef>
#include <cstring>
#include <mutex>
#include <termios.h>
#include <unistd.h>

RingBuffer::RingBuffer() : head(0), tail(0), count(0) {}

void RingBuffer::write(const uint8_t *data, size_t len){
    size_t written = 0;
    std::unique_lock<std::mutex> lock(mtx);
    while (written < len){
        not_full.wait(lock, [&]{ return count < BUFFERSIZE; });
        size_t space = BUFFERSIZE - count;
        size_t to_write = std::min(len - written, space);

        size_t end_space = BUFFERSIZE - tail;
        size_t n1 = std::min(end_space, to_write);
        std::memcpy(buf + tail, data + written, n1);
        tail = (tail + n1) % BUFFERSIZE;

        if (n1 < to_write) {
            size_t n2 = to_write - n1;
            std::memcpy(buf + tail, data + written + n1, n2);
            tail = (tail + n2) % BUFFERSIZE;
        }

        count+= to_write;
        written += to_write;
        not_empty.notify_one();
    }
}

size_t RingBuffer::read(uint8_t *out, size_t maxlen){
    std::unique_lock<std::mutex> lock(mtx);
    not_empty.wait(lock, [&]{return count > 0;});
    
    size_t to_read = std::min(count, maxlen);
    size_t end_space = BUFFERSIZE - head;
    size_t n1 = std::min(end_space, to_read);

    std::memcpy(out, buf + head, n1);
    head = (head + n1) % BUFFERSIZE;

    if (n1 < to_read){
        size_t n2 = to_read - n1;
        std::memcpy(out + n1, buf + head, n2);
        head = (head + n2) % BUFFERSIZE;
    }

    count -= to_read;
    not_full.notify_one();
    return to_read;
}
