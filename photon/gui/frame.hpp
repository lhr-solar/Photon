#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>

class frame {
public:
    uint8_t* data;
    int width;
    int height;
    int stride;
    double timestamp;
    size_t dataSize;
    int bytesPerPixel;

    frame();
    ~frame();

    // Move semantics
    frame(frame&& other) noexcept;
    frame& operator=(frame&& other) noexcept;

    // Delete copy semantics
    frame(const frame&) = delete;
    frame& operator=(const frame&) = delete;

    bool allocate(int w, int h, int s);
    void free();
    bool isValid() const;

    uint8_t* getPixel(int x, int y);
    const uint8_t* getPixel(int x, int y) const;
};