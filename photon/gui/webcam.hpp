#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef __linux__
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}
#endif

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

    // H.264 decode pipeline. SPS/PPS often arrive in the first few packets,
    // so the decoder may need a couple of frames before producing output.
    AVCodecContext* avctx = nullptr;
    AVPacket* avpkt = nullptr;
    AVFrame* avframe = nullptr;
    SwsContext* sws = nullptr;
    int swsSrcW = 0;
    int swsSrcH = 0;
    int swsSrcFmt = -1;

    // Saved so we can reopen the same device after an unplug without the
    // caller having to reinitialize.
    std::string targetDevicePath;
    uint32_t targetWidth = 0;
    uint32_t targetHeight = 0;
    int64_t lastReconnectMs = 0;

    bool openDevice();
    void closeDevice();
    bool initDevice(const std::string& devicePath, uint32_t requestWidth, uint32_t requestHeight);
    bool initMMap();
    bool startStreaming();
    bool dequeueFrame(std::vector<uint8_t>& outRGBA);
    void stopStreaming();
    void markDeviceLost();
    bool maybeReconnect();
    bool initDecoder();
    void closeDecoder();
    bool decodeH264(const uint8_t* src, size_t bytesUsed, std::vector<uint8_t>& outRGBA);
#endif
};
