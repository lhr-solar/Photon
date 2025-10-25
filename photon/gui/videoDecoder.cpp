#include "videoDecoder.hpp"
#include "frame.hpp"
#include <iostream>
#include <stdexcept>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

videoDecoder::videoDecoder() {}

videoDecoder::~videoDecoder() {
    close();
}

void videoDecoder::ffmpeg_init_once() {
    static bool initialized = false;
    if (!initialized) {
        // av_register_all() and avcodec_register_all() were removed/are no-ops in modern FFmpeg.
        // Only call them if the older version macros indicate they exist.
#if defined(LIBAVCODEC_VERSION_INT) && LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58,9,100)
        av_register_all();
        avcodec_register_all();
#endif
        initialized = true;
    }
}

bool videoDecoder::open(const std::string& filePath) {
    close();
    // Open input
    if (avformat_open_input(&formatContext, filePath.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Could not open file: " << filePath << std::endl;
        return false;
    }
    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        std::cerr << "Could not find stream info." << std::endl;
        return false;
    }

    // Find video stream
    vidStreamIdx = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vidStreamIdx < 0) {
        std::cerr << "Could not find video stream." << std::endl;
        return false;
    }

    AVStream* stream = formatContext->streams[vidStreamIdx];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        std::cerr << "Could not find decoder." << std::endl;
        return false;
    }

    codecContext = avcodec_alloc_context3(decoder);
    if (!codecContext) {
        std::cerr << "Could not allocate codec context." << std::endl;
        return false;
    }
    if (avcodec_parameters_to_context(codecContext, stream->codecpar) < 0) {
        std::cerr << "Could not copy codec parameters." << std::endl;
        return false;
    }
    if (avcodec_open2(codecContext, decoder, nullptr) < 0) {
        std::cerr << "Failed to open codec." << std::endl;
        return false;
    }

    m_width = codecContext->width;
    m_height = codecContext->height;
    m_duration = (formatContext->duration != AV_NOPTS_VALUE)
        ? (double)formatContext->duration / AV_TIME_BASE
        : 0.0;

    avFrame = av_frame_alloc();
    rgbFrame = av_frame_alloc();
    packet = av_packet_alloc();

    // Prepare RGB conversion
    swsContext = sws_getContext(
        m_width, m_height, codecContext->pix_fmt,
        m_width, m_height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    // Allocate RGB frame buffer
    int rgbStride = m_width * 3;  // RGB24 = 3 bytes/pixel
    av_image_fill_arrays(
        rgbFrame->data, rgbFrame->linesize,
        (uint8_t*)av_malloc(m_height * rgbStride),
        AV_PIX_FMT_RGB24, m_width, m_height, 1
    );
    return true;
}

bool videoDecoder::decodeNextFrame(frame& outFrame) {
    int ret;
    // Read frames until video frame found
    while ((ret = av_read_frame(formatContext, packet)) >= 0) {
        if (packet->stream_index == vidStreamIdx) {
            ret = avcodec_send_packet(codecContext, packet);
            if (ret < 0) {
                std::cerr << "Error sending packet for decoding.\n";
                av_packet_unref(packet);
                continue;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(codecContext, avFrame); // <- use avFrame
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    std::cerr << "Error during decoding.\n";
                    av_packet_unref(packet);
                    return false;
                }

                // Convert frame to RGB
                sws_scale(
                    swsContext,
                    avFrame->data,
                    avFrame->linesize,
                    0,
                    m_height,
                    rgbFrame->data,
                    rgbFrame->linesize
                );

                // Allocate target frame
                int stride = rgbFrame->linesize[0];
                if (!outFrame.allocate(m_width, m_height, stride)) {
                    std::cerr << "Failed to allocate output frame.\n";
                    av_packet_unref(packet);
                    return false;
                }

                // Copy RGB data row by row
                for (int y = 0; y < m_height; ++y) {
                    std::memcpy(
                        outFrame.data + y * stride,
                        rgbFrame->data[0] + y * rgbFrame->linesize[0],
                        stride
                    );
                }
                outFrame.timestamp = (avFrame->pts != AV_NOPTS_VALUE)
                    ? avFrame->pts * av_q2d(formatContext->streams[vidStreamIdx]->time_base)
                    : 0.0;

                av_packet_unref(packet);
                return true;
            }
        }
        av_packet_unref(packet);
    }
    return false; // No frame decoded (end of stream)
}

void videoDecoder::close() {
    if (rgbFrame) {
        if (rgbFrame->data[0]) {
            av_freep(&rgbFrame->data[0]);
        }
        av_frame_free(&rgbFrame);
        rgbFrame = nullptr;
    }
    if (avFrame) {
        av_frame_free(&avFrame);
        avFrame = nullptr;
    }
    if (packet) {
        av_packet_free(&packet);
        packet = nullptr;
    }
    if (codecContext) {
        avcodec_free_context(&codecContext);
        codecContext = nullptr;
    }
    if (formatContext) {
        avformat_close_input(&formatContext);
        formatContext = nullptr;
    }
    if (swsContext) {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }
    vidStreamIdx = -1;
    m_width = 0;
    m_height = 0;
    m_duration = 0.0;
}
