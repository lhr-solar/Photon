#pragma once
#include <string>
#include "frame.hpp"

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

class videoDecoder {
public:
    videoDecoder();
    ~videoDecoder();

    // Initialize FFmpeg (call once)
    static void ffmpeg_init_once();

    // Open video file
    bool open(const std::string& filePath);
    
    // Decode next frame
    bool decodeNextFrame(frame& outFrame);
    
    // Close and cleanup
    void close();

    // Video properties
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    double duration() const { return m_duration; }
    bool isOpen() const { return formatContext != nullptr; }

private:
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    AVFrame* avFrame = nullptr;
    AVFrame* rgbFrame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* swsContext = nullptr;
    
    int vidStreamIdx = -1;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    double m_duration = 0.0;
};