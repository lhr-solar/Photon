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

bool mjpegToRgba(tjhandle decoder, const uint8_t* src, size_t bytesUsed, uint32_t width, uint32_t height, std::vector<uint8_t>& dst) {
    dst.resize(static_cast<size_t>(width) * height * 4);
    int rc = tjDecompress2(decoder,
                           src, static_cast<unsigned long>(bytesUsed),
                           dst.data(),
                           static_cast<int>(width), 0 /*pitch=auto*/, static_cast<int>(height),
                           TJPF_RGBA, TJFLAG_FASTDCT);
    return rc == 0;
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
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (!xioctl(fd, VIDIOC_S_FMT, &fmt)) {
        logs("[!] WebcamCapture: VIDIOC_S_FMT failed error=" << strerror(errno));
        return false;
    }

    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
        logs("[!] WebcamCapture: driver did not accept MJPEG");
        return false;
    }

    frameWidth = fmt.fmt.pix.width;
    frameHeight = fmt.fmt.pix.height;

    if (frameWidth == 0 || frameHeight == 0) {
        logs("[!] WebcamCapture: invalid frame dimensions " << frameWidth << "x" << frameHeight);
        return false;
    }

    if (!jpegDecoder) {
        jpegDecoder = tjInitDecompress();
        if (!jpegDecoder) {
            logs("[!] WebcamCapture: tjInitDecompress failed: " << tjGetErrorStr());
            return false;
        }
    }

    logs("[+] WebcamCapture: using format " << frameWidth << "x" << frameHeight << " MJPEG");
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
    bool decoded = mjpegToRgba(jpegDecoder, src, buf.bytesused, frameWidth, frameHeight, outRGBA);
    if (!decoded) {
        logs("[!] WebcamCapture: MJPEG decode failed: " << tjGetErrorStr());
    }

    if (!xioctl(fd, VIDIOC_QBUF, &buf)) {
        logs("[!] WebcamCapture: VIDIOC_QBUF failed error=" << strerror(errno));
        return false;
    }

    return decoded;
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
    if (!available && fd < 0 && !jpegDecoder) { return; }
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

    if (jpegDecoder) {
        tjDestroy(jpegDecoder);
        jpegDecoder = nullptr;
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
