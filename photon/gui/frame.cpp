#include "frame.hpp"

frame::frame()
    : data(nullptr)
    , width(0)
    , height(0)
    , stride(0)
    , timestamp(0.0)
    , dataSize(0)
    , bytesPerPixel(4)
{
}

frame::~frame() {
    free();
}

frame::frame(frame&& other) noexcept
    : data(other.data)
    , width(other.width)
    , height(other.height)
    , stride(other.stride)
    , timestamp(other.timestamp)
    , dataSize(other.dataSize)
    , bytesPerPixel(other.bytesPerPixel)
{
    other.data = nullptr;
    other.dataSize = 0;
}

frame& frame::operator=(frame&& other) noexcept {
    if (this != &other) {
        free();
        data = other.data;
        width = other.width;
        height = other.height;
        stride = other.stride;
        timestamp = other.timestamp;
        dataSize = other.dataSize;
        bytesPerPixel = other.bytesPerPixel;

        other.data = nullptr;
        other.dataSize = 0;
    }
    return *this;
}

bool frame::allocate(int w, int h, int s) {
    free(); // Free any existing data

    width = w;
    height = h;
    stride = s;
    
    // Derive bytes per pixel from stride when possible
    if (width > 0 && stride >= width) {
        bytesPerPixel = stride / width;
        if (bytesPerPixel <= 0) bytesPerPixel = 4;
    } else {
        bytesPerPixel = 4; // default RGBA
    }
    
    dataSize = static_cast<size_t>(stride) * height;

    data = static_cast<uint8_t*>(::malloc(dataSize));
    if (data) {
        // Zero out the memory for safety
        std::memset(data, 0, dataSize);
        return true;
    }
    return false;
}

void frame::free() {
    if (data) {
        ::free(data);
        data = nullptr;
    }
    width = 0;
    height = 0;
    stride = 0;
    dataSize = 0;
    timestamp = 0.0;
    bytesPerPixel = 4;
}

bool frame::isValid() const {
    return data != nullptr && width > 0 && height > 0 && dataSize > 0;
}

uint8_t* frame::getPixel(int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height || !data) {
        return nullptr;
    }
    return data + (y * stride) + (x * bytesPerPixel);
}

const uint8_t* frame::getPixel(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height || !data) {
        return nullptr;
    }
    return data + (y * stride) + (x * bytesPerPixel);
}