#include "webcam.hpp"

#include "../engine/include.hpp"

#ifdef __linux__

#include <algorithm>

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

bool xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do { r = ioctl(fd, request, arg); } 
    while (r == -1 && errno == EINTR);
    return r != -1;
}

inline uint8_t clampToByte(int value) {
    if (value < 0) { return 0; }
    if (value > 255) { return 255; }
    return static_cast<uint8_t>(value);
}

void yuyvToRgba(const uint8_t* src, size_t bytesUsed, uint32_t width, uint32_t height, std::vector<uint8_t>& dst) {
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t pairCount = std::min(pixelCount / 2, bytesUsed / 4);

    dst.resize(pixelCount * 4);
    uint8_t* out = dst.data();

    for (size_t i = 0; i < pairCount; ++i) {
        const size_t srcIndex = i * 4;
        const size_t dstIndex = i * 8;

        const uint8_t y0 = src[srcIndex + 0];
        const uint8_t u  = src[srcIndex + 1];
        const uint8_t y1 = src[srcIndex + 2];
        const uint8_t v  = src[srcIndex + 3];

        const int c0 = static_cast<int>(y0) - 16;
        const int c1 = static_cast<int>(y1) - 16;
        const int d  = static_cast<int>(u) - 128;
        const int e  = static_cast<int>(v) - 128;

        const int r0 = (298 * c0 + 409 * e + 128) >> 8;
        const int g0 = (298 * c0 - 100 * d - 208 * e + 128) >> 8;
        const int b0 = (298 * c0 + 516 * d + 128) >> 8;

        const int r1 = (298 * c1 + 409 * e + 128) >> 8;
        const int g1 = (298 * c1 - 100 * d - 208 * e + 128) >> 8;
        const int b1 = (298 * c1 + 516 * d + 128) >> 8;

        out[dstIndex + 0] = clampToByte(r0);
        out[dstIndex + 1] = clampToByte(g0);
        out[dstIndex + 2] = clampToByte(b0);
        out[dstIndex + 3] = 255;

        out[dstIndex + 4] = clampToByte(r1);
        out[dstIndex + 5] = clampToByte(g1);
        out[dstIndex + 6] = clampToByte(b1);
        out[dstIndex + 7] = 255;
    }

    // Zero any trailing pixels if the camera delivered less data than expected
    const size_t convertedPixels = pairCount * 2;
    if (convertedPixels < pixelCount) {
        memset(out + convertedPixels * 4, 0, (pixelCount - convertedPixels) * 4);
    }
}

} // namespace

WebcamCapture::WebcamCapture() = default;

WebcamCapture::~WebcamCapture() {
    shutdown();
}

bool WebcamCapture::initialize(const std::string& devicePath, uint32_t requestWidth, uint32_t requestHeight) {
    shutdown();
    if (!initDevice(devicePath, requestWidth, requestHeight)) {
        shutdown();
        return false;
    }
    if (!initMMap()) {
        shutdown();
        return false;
    }
    if (!startStreaming()) {
        shutdown();
        return false;
    }
    available = true;
    return true;
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
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (!xioctl(fd, VIDIOC_S_FMT, &fmt)) {
        logs("[!] WebcamCapture: VIDIOC_S_FMT failed error=" << strerror(errno));
        return false;
    }

    frameWidth = fmt.fmt.pix.width;
    frameHeight = fmt.fmt.pix.height;

    if (frameWidth == 0 || frameHeight == 0) {
        logs("[!] WebcamCapture: invalid frame dimensions " << frameWidth << "x" << frameHeight);
        return false;
    }

    logs("[+] WebcamCapture: using format " << frameWidth << "x" << frameHeight << " YUYV");
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
    if (!available) { return false; }

    pollfd fds{};
    fds.fd = fd;
    fds.events = POLLIN;

    int pollResult = poll(&fds, 1, 0);
    if (pollResult <= 0) { return false; }

    return dequeueFrame(outRGBA);
}

bool WebcamCapture::dequeueFrame(std::vector<uint8_t>& outRGBA) {
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (!xioctl(fd, VIDIOC_DQBUF, &buf)) {
        if (errno != EAGAIN) {
            logs("[!] WebcamCapture: VIDIOC_DQBUF failed error=" << strerror(errno));
        }
        return false;
    }

    if (buf.index >= buffers.size()) {
        logs("[!] WebcamCapture: buffer index out of range");
        return false;
    }

    const uint8_t* src = static_cast<uint8_t*>(buffers[buf.index].start);
    yuyvToRgba(src, buf.bytesused, frameWidth, frameHeight, outRGBA);

    if (!xioctl(fd, VIDIOC_QBUF, &buf)) {
        logs("[!] WebcamCapture: VIDIOC_QBUF failed error=" << strerror(errno));
        return false;
    }

    return true;
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
    if (!available && fd < 0) { return; }
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
    frameWidth = 0;
    frameHeight = 0;
}

#else

WebcamCapture::WebcamCapture() = default;
WebcamCapture::~WebcamCapture() = default;

bool WebcamCapture::initialize(const std::string&, uint32_t, uint32_t) {
    logs("[!] WebcamCapture: webcam capture is only supported on Linux builds");
    available = false;
    frameWidth = 0;
    frameHeight = 0;
    return false;
}

bool WebcamCapture::captureFrame(std::vector<uint8_t>&) {
    return false;
}

void WebcamCapture::shutdown() {
    available = false;
    frameWidth = 0;
    frameHeight = 0;
}

#endif
