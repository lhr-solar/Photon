#include "webcam.hpp"

#include "../engine/include.hpp"

#include <algorithm>
#include <time.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
}

namespace {

constexpr int64_t kReconnectIntervalMs = 1000;

bool xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do { r = ioctl(fd, request, arg); }
    while (r == -1 && errno == EINTR);
    return r != -1;
}

int64_t nowMs() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

bool isDeviceLostErrno(int err) {
    return err == ENODEV || err == EIO || err == EPIPE || err == ENXIO;
}

} // namespace

WebcamCapture::WebcamCapture() = default;

WebcamCapture::~WebcamCapture() {
    shutdown();
}

bool WebcamCapture::initialize(const std::string& devicePath, uint32_t requestWidth, uint32_t requestHeight) {
    shutdown();
    targetDevicePath = devicePath;
    targetWidth = requestWidth;
    targetHeight = requestHeight;
    lastReconnectMs = 0;
    if (!initDecoder()) {
        return false;
    }
    return openDevice();
}

bool WebcamCapture::openDevice() {
    if (targetDevicePath.empty()) { return false; }
    if (!initDevice(targetDevicePath, targetWidth, targetHeight)) {
        closeDevice();
        return false;
    }
    if (!initMMap()) {
        closeDevice();
        return false;
    }
    if (!startStreaming()) {
        closeDevice();
        return false;
    }
    available = true;
    return true;
}

void WebcamCapture::closeDevice() {
    if (streaming) { stopStreaming(); }

    for (auto& buffer : buffers) {
        if (buffer.start && buffer.start != MAP_FAILED) {
            munmap(buffer.start, buffer.length);
        }
    }
    buffers.clear();

    if (fd >= 0) {
        close(fd);
        fd = -1;
    }

    available = false;
}

void WebcamCapture::markDeviceLost() {
    closeDevice();
    if (avctx) {
        avcodec_flush_buffers(avctx);
    }
}

bool WebcamCapture::maybeReconnect() {
    int64_t t = nowMs();
    if (t - lastReconnectMs < kReconnectIntervalMs) { return false; }
    lastReconnectMs = t;
    closeDevice();
    return openDevice();
}

bool WebcamCapture::initDecoder() {
    closeDecoder();

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        logs("[!] WebcamCapture: libavcodec has no H.264 decoder");
        return false;
    }

    avctx = avcodec_alloc_context3(codec);
    if (!avctx) {
        logs("[!] WebcamCapture: avcodec_alloc_context3 failed");
        return false;
    }

    if (avcodec_open2(avctx, codec, nullptr) < 0) {
        logs("[!] WebcamCapture: avcodec_open2 failed");
        closeDecoder();
        return false;
    }

    avpkt = av_packet_alloc();
    avframe = av_frame_alloc();
    if (!avpkt || !avframe) {
        logs("[!] WebcamCapture: av_packet/frame_alloc failed");
        closeDecoder();
        return false;
    }
    return true;
}

void WebcamCapture::closeDecoder() {
    if (sws) {
        sws_freeContext(sws);
        sws = nullptr;
    }
    swsSrcW = swsSrcH = 0;
    swsSrcFmt = -1;

    if (avframe) {
        av_frame_free(&avframe);
    }
    if (avpkt) {
        av_packet_free(&avpkt);
    }
    if (avctx) {
        avcodec_free_context(&avctx);
    }
}

bool WebcamCapture::initDevice(const std::string& devicePath, uint32_t requestWidth, uint32_t requestHeight) {
    fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
        logs("[!] WebcamCapture: failed to open " << devicePath << " error=" << strerror(errno));
        return false;
    }

    v4l2_capability cap{};
    if (!xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        logs("[!] WebcamCapture: VIDIOC_QUERYCAP failed error=" << strerror(errno));
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        logs("[!] WebcamCapture: device does not support video capture");
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        logs("[!] WebcamCapture: device does not support streaming I/O");
        return false;
    }

    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = requestWidth;
    fmt.fmt.pix.height = requestHeight;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (!xioctl(fd, VIDIOC_S_FMT, &fmt)) {
        logs("[!] WebcamCapture: VIDIOC_S_FMT failed error=" << strerror(errno));
        return false;
    }

    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_H264) {
        logs("[!] WebcamCapture: driver did not accept H.264");
        return false;
    }

    frameWidth = fmt.fmt.pix.width;
    frameHeight = fmt.fmt.pix.height;

    if (frameWidth == 0 || frameHeight == 0) {
        logs("[!] WebcamCapture: invalid frame dimensions " << frameWidth << "x" << frameHeight);
        return false;
    }

    // Cap the framerate so UVC picks a smaller USB altsetting. H.264 already
    // fits 3 cams comfortably on USB 2.0 vs MJPEG, but we keep the cap so
    // bandwidth has headroom for hub overhead.
    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = 15;
    if (!xioctl(fd, VIDIOC_S_PARM, &parm)) {
        logs("[!] WebcamCapture: VIDIOC_S_PARM failed error=" << strerror(errno));
        // not fatal -- fall through
    }

    logs("[+] WebcamCapture: using format " << frameWidth << "x" << frameHeight << " H264");
    return true;
}

bool WebcamCapture::initMMap() {
    v4l2_requestbuffers req{};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (!xioctl(fd, VIDIOC_REQBUFS, &req)) {
        logs("[!] WebcamCapture: VIDIOC_REQBUFS failed error=" << strerror(errno));
        return false;
    }

    if (req.count < 2) {
        logs("[!] WebcamCapture: insufficient buffer memory (" << req.count << ")");
        return false;
    }

    buffers.resize(req.count);

    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (!xioctl(fd, VIDIOC_QUERYBUF, &buf)) {
            logs("[!] WebcamCapture: VIDIOC_QUERYBUF failed error=" << strerror(errno));
            return false;
        }

        buffers[i].length = buf.length;
        buffers[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            logs("[!] WebcamCapture: mmap failed error=" << strerror(errno));
            return false;
        }

        if (!xioctl(fd, VIDIOC_QBUF, &buf)) {
            logs("[!] WebcamCapture: VIDIOC_QBUF failed error=" << strerror(errno));
            return false;
        }
    }

    return true;
}

bool WebcamCapture::startStreaming() {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (!xioctl(fd, VIDIOC_STREAMON, &type)) {
        logs("[!] WebcamCapture: VIDIOC_STREAMON failed error=" << strerror(errno));
        return false;
    }
    streaming = true;
    return true;
}

bool WebcamCapture::captureFrame(std::vector<uint8_t>& outRGBA) {
    if (!available) {
        if (!maybeReconnect()) { return false; }
    }

    pollfd fds{};
    fds.fd = fd;
    fds.events = POLLIN;

    int pollResult = poll(&fds, 1, 0);
    if (pollResult < 0) {
        if (isDeviceLostErrno(errno)) { markDeviceLost(); }
        return false;
    }
    if (pollResult == 0) { return false; }
    if (fds.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        markDeviceLost();
        return false;
    }

    return dequeueFrame(outRGBA);
}

bool WebcamCapture::dequeueFrame(std::vector<uint8_t>& outRGBA) {
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (!xioctl(fd, VIDIOC_DQBUF, &buf)) {
        int err = errno;
        if (err == EAGAIN) { return false; }
        logs("[!] WebcamCapture: VIDIOC_DQBUF failed error=" << strerror(err));
        if (isDeviceLostErrno(err)) { markDeviceLost(); }
        return false;
    }

    if (buf.index >= buffers.size()) {
        logs("[!] WebcamCapture: buffer index out of range");
        return false;
    }

    const uint8_t* src = static_cast<uint8_t*>(buffers[buf.index].start);
    bool decoded = decodeH264(src, buf.bytesused, outRGBA);

    if (!xioctl(fd, VIDIOC_QBUF, &buf)) {
        int err = errno;
        logs("[!] WebcamCapture: VIDIOC_QBUF failed error=" << strerror(err));
        if (isDeviceLostErrno(err)) { markDeviceLost(); }
        return false;
    }

    return decoded;
}

bool WebcamCapture::decodeH264(const uint8_t* src, size_t bytesUsed, std::vector<uint8_t>& outRGBA) {
    if (!avctx || !avpkt || !avframe) { return false; }
    if (bytesUsed == 0) { return false; }

    av_packet_unref(avpkt);
    avpkt->data = const_cast<uint8_t*>(src);
    avpkt->size = static_cast<int>(bytesUsed);

    int sret = avcodec_send_packet(avctx, avpkt);
    if (sret < 0 && sret != AVERROR(EAGAIN)) {
        // Bad packet -- skip this frame, decoder stays usable.
        return false;
    }

    int rret = avcodec_receive_frame(avctx, avframe);
    if (rret == AVERROR(EAGAIN)) {
        // Decoder needs more packets (SPS/PPS, B-frames). Normal during the
        // first ~1-3 frames after a stream starts.
        return false;
    }
    if (rret < 0) {
        return false;
    }

    int srcW = avframe->width;
    int srcH = avframe->height;
    int srcFmt = avframe->format;
    if (srcW <= 0 || srcH <= 0) { return false; }

    if (!sws || srcW != swsSrcW || srcH != swsSrcH || srcFmt != swsSrcFmt
            || srcW != static_cast<int>(frameWidth)
            || srcH != static_cast<int>(frameHeight)) {
        if (sws) { sws_freeContext(sws); sws = nullptr; }
        sws = sws_getContext(srcW, srcH, static_cast<AVPixelFormat>(srcFmt),
                             static_cast<int>(frameWidth), static_cast<int>(frameHeight),
                             AV_PIX_FMT_RGBA,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws) {
            logs("[!] WebcamCapture: sws_getContext failed");
            return false;
        }
        swsSrcW = srcW;
        swsSrcH = srcH;
        swsSrcFmt = srcFmt;
    }

    outRGBA.resize(static_cast<size_t>(frameWidth) * frameHeight * 4);
    uint8_t* dstData[1] = { outRGBA.data() };
    int dstStride[1] = { static_cast<int>(frameWidth) * 4 };
    int scaled = sws_scale(sws, avframe->data, avframe->linesize, 0, srcH,
                           dstData, dstStride);
    return scaled > 0;
}

void WebcamCapture::stopStreaming() {
    if (!streaming) { return; }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (!xioctl(fd, VIDIOC_STREAMOFF, &type)) {
        logs("[!] WebcamCapture: VIDIOC_STREAMOFF failed error=" << strerror(errno));
    }
    streaming = false;
}

void WebcamCapture::shutdown() {
    closeDevice();
    closeDecoder();

    targetDevicePath.clear();
    targetWidth = 0;
    targetHeight = 0;
    lastReconnectMs = 0;
    frameWidth = 0;
    frameHeight = 0;
}
