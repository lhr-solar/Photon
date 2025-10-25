#include "frame.hpp"

frame::frame()
    : data(nullptr)
    , width(0)
    , height(0)
    , stride(0)
    , timestamp(0.0)
    , dataSize(0)
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
    dataSize = static_cast<size_t>(stride) * height;

    data = static_cast<uint8_t*>(::malloc(dataSize));
    return data != nullptr;
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
}

bool frame::isValid() const {
    return data != nullptr && width > 0 && height > 0;
}

uint8_t* frame::getPixel(int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height || !data) {
        return nullptr;
    }
    return data + (y * stride) + (x * 3); // RGB24 = 3 bytes per pixel
}

const uint8_t* frame::getPixel(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height || !data) {
        return nullptr;
    }
    return data + (y * stride) + (x * 3);
}
