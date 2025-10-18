#pragma once

#include <cstdint>
#include <string>
#include <vector>

class FFmpegVideoDecoder {
public:
    FFmpegVideoDecoder();
    ~FFmpegVideoDecoder();

    bool open(const std::string& filePath);
    bool readFrame(std::vector<uint8_t>& rgbaFrame);
    void reset();

    int width() const { return width_; }
    int height() const { return height_; }
    double frameRate() const { return frameRate_; }
    bool isOpen() const { return initialized_; }

private:
    bool initializeSwsContext(int width, int height, int pixFormat);
    void cleanup();

    struct AVFormatContext* formatCtx_ = nullptr;
    struct AVCodecContext* codecCtx_ = nullptr;
    struct SwsContext* swsCtx_ = nullptr;
    struct AVFrame* frame_ = nullptr;
    struct AVFrame* rgbaFrame_ = nullptr;
    struct AVPacket* packet_ = nullptr;
    uint8_t* rgbaBuffer_ = nullptr;

    int videoStreamIndex_ = -1;
    int width_ = 0;
    int height_ = 0;
    double frameRate_ = 0.0;
    bool initialized_ = false;
    bool flushing_ = false;
};
