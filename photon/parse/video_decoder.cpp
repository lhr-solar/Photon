#include "video_decoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

#include <algorithm>

FFmpegVideoDecoder::FFmpegVideoDecoder() = default;

FFmpegVideoDecoder::~FFmpegVideoDecoder() {
    cleanup();
}

bool FFmpegVideoDecoder::open(const std::string& filePath) {
    cleanup();

    if (avformat_open_input(&formatCtx_, filePath.c_str(), nullptr, nullptr) < 0) {
        return false;
    }

    if (avformat_find_stream_info(formatCtx_, nullptr) < 0) {
        cleanup();
        return false;
    }

    videoStreamIndex_ = av_find_best_stream(formatCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex_ < 0) {
        cleanup();
        return false;
    }

    AVStream* stream = formatCtx_->streams[videoStreamIndex_];
    AVCodecParameters* codecPar = stream->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        cleanup();
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        cleanup();
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx_, codecPar) < 0) {
        cleanup();
        return false;
    }

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        cleanup();
        return false;
    }

    width_ = codecCtx_->width;
    height_ = codecCtx_->height;

    AVRational rate = av_guess_frame_rate(formatCtx_, stream, nullptr);
    if (rate.num > 0 && rate.den > 0) {
        frameRate_ = av_q2d(rate);
    } else if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
        frameRate_ = av_q2d(stream->avg_frame_rate);
    } else {
        frameRate_ = 30.0;
    }

    frame_ = av_frame_alloc();
    rgbaFrame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!frame_ || !rgbaFrame_ || !packet_) {
        cleanup();
        return false;
    }

    int bufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width_, height_, 1);
    rgbaBuffer_ = static_cast<uint8_t*>(av_malloc(bufferSize));
    if (!rgbaBuffer_) {
        cleanup();
        return false;
    }

    if (av_image_fill_arrays(rgbaFrame_->data, rgbaFrame_->linesize, rgbaBuffer_, AV_PIX_FMT_RGBA, width_, height_, 1) < 0) {
        cleanup();
        return false;
    }

    if (!initializeSwsContext(width_, height_, codecCtx_->pix_fmt)) {
        cleanup();
        return false;
    }

    initialized_ = true;
    flushing_ = false;
    return true;
}

bool FFmpegVideoDecoder::initializeSwsContext(int width, int height, int pixFormat) {
    swsCtx_ = sws_getContext(width,
                             height,
                             static_cast<AVPixelFormat>(pixFormat),
                             width,
                             height,
                             AV_PIX_FMT_RGBA,
                             SWS_BILINEAR,
                             nullptr,
                             nullptr,
                             nullptr);
    return swsCtx_ != nullptr;
}

bool FFmpegVideoDecoder::readFrame(std::vector<uint8_t>& rgbaFrame) {
    if (!initialized_) {
        return false;
    }

    rgbaFrame.clear();

    auto convertFrame = [&]() -> bool {
        if (sws_scale(swsCtx_, frame_->data, frame_->linesize, 0, height_, rgbaFrame_->data, rgbaFrame_->linesize) <= 0) {
            return false;
        }

        rgbaFrame.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4);
        for (int y = 0; y < height_; ++y) {
            const uint8_t* src = rgbaFrame_->data[0] + y * rgbaFrame_->linesize[0];
            uint8_t* dst = rgbaFrame.data() + static_cast<size_t>(y) * static_cast<size_t>(width_) * 4;
            std::copy_n(src, static_cast<size_t>(width_) * 4, dst);
        }
        return true;
    };

    while (true) {
        if (!flushing_) {
            int readStatus = av_read_frame(formatCtx_, packet_);
            if (readStatus == AVERROR_EOF) {
                flushing_ = true;
                av_packet_unref(packet_);
            } else if (readStatus < 0) {
                return false;
            } else {
                if (packet_->stream_index != videoStreamIndex_) {
                    av_packet_unref(packet_);
                    continue;
                }

                int sendStatus = avcodec_send_packet(codecCtx_, packet_);
                av_packet_unref(packet_);
                if (sendStatus == AVERROR(EAGAIN)) {
                    continue;
                } else if (sendStatus < 0) {
                    return false;
                }
            }
        }

        if (flushing_) {
            int sendStatus = avcodec_send_packet(codecCtx_, nullptr);
            if (sendStatus == AVERROR_EOF) {
                return false;
            } else if (sendStatus == AVERROR(EAGAIN)) {
                // Need to receive remaining frames before sending another flush packet
            } else if (sendStatus < 0) {
                return false;
            }
        }

        while (true) {
            int receiveStatus = avcodec_receive_frame(codecCtx_, frame_);
            if (receiveStatus == AVERROR(EAGAIN)) {
                break;
            }
            if (receiveStatus == AVERROR_EOF) {
                return false;
            }
            if (receiveStatus < 0) {
                return false;
            }

            if (convertFrame()) {
                return true;
            }
        }
    }
}


void FFmpegVideoDecoder::reset() {
    if (!initialized_) {
        return;
    }

    avcodec_flush_buffers(codecCtx_);
    av_seek_frame(formatCtx_, videoStreamIndex_, 0, AVSEEK_FLAG_BACKWARD);
    flushing_ = false;
}

void FFmpegVideoDecoder::cleanup() {
    initialized_ = false;
    flushing_ = false;

    if (packet_) {
        av_packet_free(&packet_);
    }

    if (frame_) {
        av_frame_free(&frame_);
    }

    if (rgbaFrame_) {
        av_frame_free(&rgbaFrame_);
    }

    if (rgbaBuffer_) {
        av_free(rgbaBuffer_);
        rgbaBuffer_ = nullptr;
    }

    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }

    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
    }

    if (formatCtx_) {
        avformat_close_input(&formatCtx_);
    }
}
