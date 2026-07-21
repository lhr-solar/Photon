#include "webcam.hpp"

#include "../engine/include.hpp"

#ifdef __linux__

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

namespace {

constexpr int64_t kReconnectIntervalMs = 1000;

bool xioctl(int fd, unsigned long request, void* arg) {
  int result;
  do {
    result = ioctl(fd, request, arg);
  } while (result == -1 && errno == EINTR);
  return result != -1;
}

bool mjpegToRgba(tjhandle decoder, const uint8_t* source, size_t bytesUsed, uint32_t width,
                 uint32_t height, std::vector<uint8_t>& destination) {
  destination.resize(static_cast<size_t>(width) * height * 4);
  return tjDecompress2(decoder, source, static_cast<unsigned long>(bytesUsed), destination.data(),
                       static_cast<int>(width), 0, static_cast<int>(height), TJPF_RGBA,
                       TJFLAG_FASTDCT) == 0;
}

int64_t nowMs() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

bool isDeviceLostErrno(int error) {
  return error == ENODEV || error == EIO || error == EPIPE || error == ENXIO;
}

}  // namespace

WebcamCapture::WebcamCapture() = default;
WebcamCapture::~WebcamCapture() { shutdown(); }

bool WebcamCapture::initialize(const std::string& devicePath, uint32_t requestWidth,
                               uint32_t requestHeight) {
  shutdown();
  targetDevicePath = devicePath;
  targetWidth = requestWidth;
  targetHeight = requestHeight;
  lastReconnectMs = 0;
  return openDevice();
}

bool WebcamCapture::openDevice() {
  if (targetDevicePath.empty()) return false;
  if (!initDevice(targetDevicePath, targetWidth, targetHeight) || !initMMap() || !startStreaming()) {
    closeDevice();
    return false;
  }
  available.store(true, std::memory_order_release);
  return true;
}

void WebcamCapture::closeDevice() {
  if (streaming) stopStreaming();
  for (auto& buffer : buffers) {
    if (buffer.start && buffer.start != MAP_FAILED) munmap(buffer.start, buffer.length);
  }
  buffers.clear();
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }
  available.store(false, std::memory_order_release);
}

void WebcamCapture::markDeviceLost() { closeDevice(); }

bool WebcamCapture::maybeReconnect() {
  const int64_t current = nowMs();
  if (current - lastReconnectMs < kReconnectIntervalMs) return false;
  lastReconnectMs = current;
  closeDevice();
  return openDevice();
}

bool WebcamCapture::initDevice(const std::string& devicePath, uint32_t requestWidth,
                               uint32_t requestHeight) {
  fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK, 0);
  if (fd < 0) {
    logs("[!] WebcamCapture: failed to open " << devicePath << " error=" << strerror(errno));
    return false;
  }

  v4l2_capability capability{};
  if (!xioctl(fd, VIDIOC_QUERYCAP, &capability) ||
      !(capability.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
      !(capability.capabilities & V4L2_CAP_STREAMING)) {
    logs("[!] WebcamCapture: device does not support streaming video capture");
    return false;
  }

  v4l2_format format{};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  format.fmt.pix.width = requestWidth;
  format.fmt.pix.height = requestHeight;
  format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
  format.fmt.pix.field = V4L2_FIELD_NONE;
  if (!xioctl(fd, VIDIOC_S_FMT, &format) || format.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
    logs("[!] WebcamCapture: driver did not accept MJPEG");
    return false;
  }

  frameWidth = format.fmt.pix.width;
  frameHeight = format.fmt.pix.height;
  if (frameWidth == 0 || frameHeight == 0) return false;

  v4l2_streamparm parameters{};
  parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parameters.parm.capture.timeperframe.numerator = 1;
  parameters.parm.capture.timeperframe.denominator = 15;
  if (!xioctl(fd, VIDIOC_S_PARM, &parameters))
    logs("[!] WebcamCapture: could not set 15 FPS: " << strerror(errno));

  if (!jpegDecoder) {
    jpegDecoder = tjInitDecompress();
    if (!jpegDecoder) {
      logs("[!] WebcamCapture: tjInitDecompress failed: " << tjGetErrorStr());
      return false;
    }
  }
  logs("[+] WebcamCapture: using " << frameWidth << "x" << frameHeight << " MJPEG");
  return true;
}

bool WebcamCapture::initMMap() {
  v4l2_requestbuffers request{};
  request.count = 2;
  request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  request.memory = V4L2_MEMORY_MMAP;
  if (!xioctl(fd, VIDIOC_REQBUFS, &request) || request.count < 2) return false;

  buffers.resize(request.count);
  for (uint32_t index = 0; index < request.count; ++index) {
    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = index;
    if (!xioctl(fd, VIDIOC_QUERYBUF, &buffer)) return false;
    buffers[index].length = buffer.length;
    buffers[index].start = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                                buffer.m.offset);
    if (buffers[index].start == MAP_FAILED || !xioctl(fd, VIDIOC_QBUF, &buffer)) return false;
  }
  return true;
}

bool WebcamCapture::startStreaming() {
  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (!xioctl(fd, VIDIOC_STREAMON, &type)) return false;
  streaming = true;
  return true;
}

bool WebcamCapture::captureFrame(std::vector<uint8_t>& outRGBA) {
  if (!isAvailable() && !maybeReconnect()) return false;

  // Drain the V4L2 queue, requeueing every buffer except the newest, then decode
  // only that last frame. Avoids multi-frame MJPEG spikes on USB glitches.
  v4l2_buffer newest{};
  bool haveNewest = false;
  for (;;) {
    pollfd descriptors{};
    descriptors.fd = fd;
    descriptors.events = POLLIN;
    const int result = poll(&descriptors, 1, 0);
    if (result < 0) {
      if (isDeviceLostErrno(errno)) markDeviceLost();
      break;
    }
    if (result == 0) break;
    if (descriptors.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      markDeviceLost();
      return false;
    }

    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (!xioctl(fd, VIDIOC_DQBUF, &buffer)) {
      const int error = errno;
      if (error != EAGAIN) {
        logs("[!] WebcamCapture: VIDIOC_DQBUF failed error=" << strerror(error));
        if (isDeviceLostErrno(error)) markDeviceLost();
      }
      break;
    }
    if (buffer.index >= buffers.size()) {
      markDeviceLost();
      return false;
    }

    if (haveNewest) {
      if (!xioctl(fd, VIDIOC_QBUF, &newest)) {
        const int error = errno;
        logs("[!] WebcamCapture: VIDIOC_QBUF failed error=" << strerror(error));
        if (isDeviceLostErrno(error)) markDeviceLost();
        return false;
      }
    }
    newest = buffer;
    haveNewest = true;
  }

  if (!haveNewest) return false;

  const auto* source = static_cast<const uint8_t*>(buffers[newest.index].start);
  const bool decoded =
      mjpegToRgba(jpegDecoder, source, newest.bytesused, frameWidth, frameHeight, outRGBA);
  if (!decoded) logs("[!] WebcamCapture: MJPEG decode failed: " << tjGetErrorStr());
  if (!xioctl(fd, VIDIOC_QBUF, &newest)) {
    const int error = errno;
    logs("[!] WebcamCapture: VIDIOC_QBUF failed error=" << strerror(error));
    if (isDeviceLostErrno(error)) markDeviceLost();
    return false;
  }
  return decoded;
}

void WebcamCapture::stopStreaming() {
  if (!streaming) return;
  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (!xioctl(fd, VIDIOC_STREAMOFF, &type))
    logs("[!] WebcamCapture: VIDIOC_STREAMOFF failed error=" << strerror(errno));
  streaming = false;
}

void WebcamCapture::shutdown() {
  closeDevice();
  if (jpegDecoder) {
    tjDestroy(jpegDecoder);
    jpegDecoder = nullptr;
  }
  targetDevicePath.clear();
  targetWidth = targetHeight = 0;
  lastReconnectMs = 0;
  frameWidth = frameHeight = 0;
}

#else

WebcamCapture::WebcamCapture() = default;
WebcamCapture::~WebcamCapture() = default;
bool WebcamCapture::initialize(const std::string&, uint32_t, uint32_t) { return false; }
bool WebcamCapture::captureFrame(std::vector<uint8_t>&) { return false; }
void WebcamCapture::shutdown() {
  available.store(false, std::memory_order_release);
  frameWidth = frameHeight = 0;
}

#endif
