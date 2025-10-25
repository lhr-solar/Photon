#pragma once

#include <stdint.h>
#include <cstdint>
#include <string.h>
#include <string>
#include "frame.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

class videoDecoder {
public:
    videoDecoder();
    ~videoDecoder();

    bool open(const std::string& filePath);
    bool decodeNextFrame(frame& outputFrame);
    void close();

    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    double getFrameDuration() const { return m_duration; }
    void ffmpeg_init_once();

private:
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    AVFrame* avFrame = nullptr;        // <-- renamed from 'frame'
    AVFrame* rgbFrame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* swsContext = nullptr;

    int vidStreamIdx = -1;
    int m_width = 0;
    int m_height = 0;
    double m_duration = 0.0;
};
