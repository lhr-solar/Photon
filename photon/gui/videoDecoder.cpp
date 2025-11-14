#include "videoDecoder.hpp"
#include "frame.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>

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
    
    // Ensure FFmpeg global init (no-op on modern FFmpeg but harmless)
    ffmpeg_init_once();
    
    // Open input
    if (avformat_open_input(&formatContext, filePath.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "[videoDecoder] Could not open file: " << filePath << std::endl;
        return false;
    }
    
    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        std::cerr << "[videoDecoder] Could not find stream info." << std::endl;
        avformat_close_input(&formatContext);
        return false;
    }

    // Find video stream
    vidStreamIdx = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vidStreamIdx < 0) {
        std::cerr << "[videoDecoder] Could not find video stream." << std::endl;
        avformat_close_input(&formatContext);
        return false;
    }

    AVStream* stream = formatContext->streams[vidStreamIdx];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        std::cerr << "[videoDecoder] Could not find decoder." << std::endl;
        avformat_close_input(&formatContext);
        return false;
    }

    codecContext = avcodec_alloc_context3(decoder);
    if (!codecContext) {
        std::cerr << "[videoDecoder] Could not allocate codec context." << std::endl;
        avformat_close_input(&formatContext);
        return false;
    }
    
    if (avcodec_parameters_to_context(codecContext, stream->codecpar) < 0) {
        std::cerr << "[videoDecoder] Could not copy codec parameters." << std::endl;
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return false;
    }
    
    if (avcodec_open2(codecContext, decoder, nullptr) < 0) {
        std::cerr << "[videoDecoder] Failed to open codec." << std::endl;
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
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
    
    if (!avFrame || !rgbFrame || !packet) {
        std::cerr << "[videoDecoder] Failed to allocate FFmpeg structures (frame/packet)." << std::endl;
        close();
        return false;
    }

    // Prepare RGBA conversion (4 bytes/pixel)
    swsContext = sws_getContext(
        m_width, m_height, codecContext->pix_fmt,
        m_width, m_height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    
    if (!swsContext) {
        std::cerr << "[videoDecoder] Failed to create swsContext for conversion." << std::endl;
        close();
        return false;
    }

    // Allocate buffer for RGBA frame with proper alignment
    int ret = av_image_alloc(
        rgbFrame->data,
        rgbFrame->linesize,
        m_width,
        m_height,
        AV_PIX_FMT_RGBA,
        32  // 32-byte alignment for better performance
    );
    
    if (ret < 0) {
        std::cerr << "[videoDecoder] av_image_alloc failed: " << ret << std::endl;
        close();
        return false;
    }
    
    std::cout << "[videoDecoder] Successfully opened: " << filePath << std::endl;
    std::cout << "[videoDecoder] Resolution: " << m_width << "x" << m_height << std::endl;
    std::cout << "[videoDecoder] Duration: " << m_duration << " seconds" << std::endl;
    std::cout << "[videoDecoder] RGBA stride: " << rgbFrame->linesize[0] << std::endl;
    
    return true;
}

bool videoDecoder::decodeNextFrame(frame& outFrame) {
    if (!formatContext || !codecContext) {
        std::cerr << "[videoDecoder] Decoder not initialized" << std::endl;
        return false;
    }
    
    int ret;
    // Read frames until video frame found
    while ((ret = av_read_frame(formatContext, packet)) >= 0) {
        if (packet->stream_index == vidStreamIdx) {
            ret = avcodec_send_packet(codecContext, packet);
            if (ret < 0) {
                std::cerr << "[videoDecoder] Error sending packet for decoding: " << ret << std::endl;
                av_packet_unref(packet);
                continue;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(codecContext, avFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    std::cerr << "[videoDecoder] Error during decoding: " << ret << std::endl;
                    av_packet_unref(packet);
                    return false;
                }

                // Convert frame to RGBA
                int convertedHeight = sws_scale(
                    swsContext,
                    avFrame->data,
                    avFrame->linesize,
                    0,
                    m_height,
                    rgbFrame->data,
                    rgbFrame->linesize
                );
                
                if (convertedHeight != m_height) {
                    std::cerr << "[videoDecoder] sws_scale conversion error: expected " 
                              << m_height << " lines, got " << convertedHeight << std::endl;
                    av_packet_unref(packet);
                    return false;
                }

                // Use the actual stride from FFmpeg (includes alignment padding)
                int actualStride = rgbFrame->linesize[0];
                
                // Allocate output frame with FFmpeg's stride
                if (!outFrame.allocate(m_width, m_height, actualStride)) {
                    std::cerr << "[videoDecoder] Failed to allocate output frame." << std::endl;
                    av_packet_unref(packet);
                    return false;
                }

                // Copy data row by row
                // We copy only the actual pixel data width, not the full stride
                // (stride may include padding bytes for alignment)
                int rowBytes = m_width * 4; // RGBA = 4 bytes per pixel
                
                for (int y = 0; y < m_height; ++y) {
                    std::memcpy(
                        outFrame.data + y * actualStride,
                        rgbFrame->data[0] + y * rgbFrame->linesize[0],
                        rowBytes  // Copy only actual pixel data, not padding
                    );
                }
                
                // Set timestamp
                outFrame.timestamp = (avFrame->pts != AV_NOPTS_VALUE)
                    ? avFrame->pts * av_q2d(formatContext->streams[vidStreamIdx]->time_base)
                    : 0.0;

                av_packet_unref(packet);
                return true;
            }
        }
        av_packet_unref(packet);
    }
    
    // End of stream
    return false;
}

void videoDecoder::close() {
    if (rgbFrame) {
        if (rgbFrame->data[0]) {
            av_freep(&rgbFrame->data[0]);  // Free buffer allocated by av_image_alloc
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
    if (swsContext) {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }
    if (codecContext) {
        avcodec_free_context(&codecContext);
        codecContext = nullptr;
    }
    if (formatContext) {
        avformat_close_input(&formatContext);
        formatContext = nullptr;
    }
    
    vidStreamIdx = -1;
    m_width = 0;
    m_height = 0;
    m_duration = 0.0;
}