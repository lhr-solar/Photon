#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <cstdint>
#include <cstdlib>

struct frame {
    uint8_t* data;          // RGB24)
    int width;
    int height;
    int stride;             // Bytes/row
    double timestamp;
    size_t dataSize;        // Total size of data buffer in bytes

    frame();
    ~frame();

    frame(const frame&) = delete;
    frame& operator=(const frame&) = delete;
    frame(frame&& other) noexcept;
    frame& operator=(frame&& other) noexcept;

    bool allocate(int w, int h, int s);
    void free();

    bool isValid() const;

    uint8_t* getPixel(int x, int y);
    const uint8_t* getPixel(int x, int y) const;
};
