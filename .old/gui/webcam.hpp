#pragma once

#include <cstdint>
#include <string>
#include <vector>

class WebcamCapture {
public:
    WebcamCapture();
    ~WebcamCapture();

    bool initialize(const std::string& devicePath, uint32_t requestWidth, uint32_t requestHeight);
    void shutdown();

    bool isAvailable() const { return available; }
    bool captureFrame(std::vector<uint8_t>& outRGBA);

    uint32_t width() const { return frameWidth; }
    uint32_t height() const { return frameHeight; }

private:
    bool available = false;
    uint32_t frameWidth = 0;
    uint32_t frameHeight = 0;

#ifdef __linux__
    struct Buffer {
        void* start = nullptr;
        size_t length = 0;
    };

    std::vector<Buffer> buffers;
    int fd = -1;
    bool streaming = false;

    bool initDevice(const std::string& devicePath, uint32_t requestWidth, uint32_t requestHeight);
    bool initMMap();
    bool startStreaming();
    bool dequeueFrame(std::vector<uint8_t>& outRGBA);
    void stopStreaming();
#endif
};
