#include "video_ui.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

#include "imgui_internal.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

static constexpr char SERVER_IP[] = "3.141.38.115";
static constexpr uint16_t SERVER_PORT = 6600;
static constexpr int SOCKET_RECEIVE_BUFFER_SIZE = 4 * 1024 * 1024;
static constexpr uint8_t RTP_PAYLOAD_TYPE = 96;
static constexpr uint8_t START_CODE[] = {0, 0, 0, 1};
static constexpr size_t MAX_ACCESS_UNIT_SIZE = 16 * 1024 * 1024;
static constexpr int MAX_VIDEO_DIMENSION = 4096;
static constexpr auto STREAM_TIMEOUT = std::chrono::seconds(1);

static SocketHandle nativeSocket(uintptr_t handle) { return static_cast<SocketHandle>(handle); }

static bool initializeSockets() {
#ifdef _WIN32
  WSADATA data{};
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
  return true;
#endif
}

static void shutdownSockets() {
#ifdef _WIN32
  WSACleanup();
#endif
}

static void closeSocket(SocketHandle socket) {
#ifdef _WIN32
  closesocket(socket);
#else
  close(socket);
#endif
}

static bool setNonBlocking(SocketHandle socket) {
#ifdef _WIN32
  u_long mode = 1;
  return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
  const int flags = fcntl(socket, F_GETFL, 0);
  return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static bool socketWouldBlock() {
#ifdef _WIN32
  return WSAGetLastError() == WSAEWOULDBLOCK;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static bool socketInterrupted() {
#ifdef _WIN32
  return WSAGetLastError() == WSAEINTR;
#else
  return errno == EINTR;
#endif
}

static bool socketReset() {
#ifdef _WIN32
  const int error = WSAGetLastError();
  return error == WSAECONNRESET || error == WSAENETRESET;
#else
  return errno == ECONNREFUSED || errno == ECONNRESET;
#endif
}

static int sendSocket(SocketHandle socket, const char* data, int size) {
  return send(socket, data, size, 0);
}

static int receiveSocket(SocketHandle socket, uint8_t* data, int capacity) {
#ifdef _WIN32
  return recv(socket, reinterpret_cast<char*>(data), capacity, 0);
#else
  return static_cast<int>(recv(socket, data, capacity, 0));
#endif
}

static int waitForSocket(SocketHandle socket) {
  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(socket, &readSet);
  timeval timeout{0, 10000};
#ifdef _WIN32
  return select(0, &readSet, nullptr, nullptr, &timeout);
#else
  return select(socket + 1, &readSet, nullptr, nullptr, &timeout);
#endif
}

static uint16_t readU16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0] << 8 | data[1]);
}

static uint32_t readU32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) << 24 | static_cast<uint32_t>(data[1]) << 16 |
         static_cast<uint32_t>(data[2]) << 8 | data[3];
}

static bool appendNal(std::vector<uint8_t>& accessUnit, const uint8_t* data, size_t size) {
  if (accessUnit.size() > MAX_ACCESS_UNIT_SIZE ||
      MAX_ACCESS_UNIT_SIZE - accessUnit.size() < sizeof(START_CODE) ||
      size > MAX_ACCESS_UNIT_SIZE - accessUnit.size() - sizeof(START_CODE))
    return false;
  accessUnit.insert(accessUnit.end(), std::begin(START_CODE), std::end(START_CODE));
  accessUnit.insert(accessUnit.end(), data, data + size);
  return true;
}

static AVPixelFormat normalizePixelFormat(AVPixelFormat format) {
  switch (format) {
    case AV_PIX_FMT_YUVJ420P:
      return AV_PIX_FMT_YUV420P;
    case AV_PIX_FMT_YUVJ411P:
      return AV_PIX_FMT_YUV411P;
    case AV_PIX_FMT_YUVJ422P:
      return AV_PIX_FMT_YUV422P;
    case AV_PIX_FMT_YUVJ444P:
      return AV_PIX_FMT_YUV444P;
    case AV_PIX_FMT_YUVJ440P:
      return AV_PIX_FMT_YUV440P;
    default:
      return format;
  }
}

static int swsColorSpace(AVColorSpace colorSpace) {
  switch (colorSpace) {
    case AVCOL_SPC_BT709:
      return SWS_CS_ITU709;
    case AVCOL_SPC_FCC:
      return SWS_CS_FCC;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
      return SWS_CS_ITU601;
    case AVCOL_SPC_SMPTE240M:
      return SWS_CS_SMPTE240M;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
      return SWS_CS_BT2020;
    default:
      return SWS_CS_DEFAULT;
  }
}

VideoUI::~VideoUI() { stop(); }

bool VideoUI::init() {
  if (backendThread.joinable()) return false;
  stopRequested.store(false, std::memory_order_relaxed);
  feedStatus.store(VideoFeedStatus::Connecting, std::memory_order_relaxed);
  try {
    backendThread = std::thread(&VideoUI::backendLoop, this);
  } catch (...) {
    feedStatus.store(VideoFeedStatus::Error, std::memory_order_relaxed);
    return false;
  }
  return true;
}

void VideoUI::stop() {
  stopRequested.store(true, std::memory_order_relaxed);
  if (backendThread.joinable()) backendThread.join();
}

void VideoUI::shutdownBackend() {
  if (socketHandle != UINTPTR_MAX) {
    sendSocket(nativeSocket(socketHandle), "bye", 3);
    closeSocket(nativeSocket(socketHandle));
    socketHandle = UINTPTR_MAX;
  }
  if (socketSubsystemInitialized) {
    shutdownSockets();
    socketSubsystemInitialized = false;
  }
  sws_freeContext(scaler);
  scaler = nullptr;
  av_frame_free(&frame);
  av_packet_free(&packet);
  avcodec_free_context(&decoderContext);
}

bool VideoUI::initBackend() {
  av_log_set_level(AV_LOG_QUIET);

  if (!initializeSockets()) return false;
  socketSubsystemInitialized = true;

  const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!decoder) return false;
  decoderContext = avcodec_alloc_context3(decoder);
  if (decoderContext) {
    decoderContext->pkt_timebase = {1, 90000};
    decoderContext->thread_count = 2;
  }
  if (!decoderContext || avcodec_open2(decoderContext, decoder, nullptr) < 0) return false;

  frame = av_frame_alloc();
  packet = av_packet_alloc();
  if (!frame || !packet) return false;

  const SocketHandle socket = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (socket == INVALID_SOCKET) return false;
  socketHandle = static_cast<uintptr_t>(socket);
#ifdef _WIN32
  setsockopt(socket, SOL_SOCKET, SO_RCVBUF,
             reinterpret_cast<const char*>(&SOCKET_RECEIVE_BUFFER_SIZE),
             sizeof(SOCKET_RECEIVE_BUFFER_SIZE));
#else
  setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &SOCKET_RECEIVE_BUFFER_SIZE,
             sizeof(SOCKET_RECEIVE_BUFFER_SIZE));
#endif
  sockaddr_in server{};
  server.sin_family = AF_INET;
  server.sin_port = htons(SERVER_PORT);
  if (inet_pton(AF_INET, SERVER_IP, &server.sin_addr) != 1 ||
      connect(socket, reinterpret_cast<sockaddr*>(&server), sizeof(server)) == SOCKET_ERROR ||
      !setNonBlocking(socket))
    return false;

  accessUnit.reserve(1024 * 1024);
  sendSocket(socket, "videoClient keepalive", 21);
  nextKeepalive = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  feedStatus.store(VideoFeedStatus::WaitingForStream, std::memory_order_relaxed);
  return true;
}

void VideoUI::backendLoop() {
  try {
    if (!initBackend()) {
      feedStatus.store(VideoFeedStatus::Error, std::memory_order_relaxed);
      shutdownBackend();
      return;
    }
    while (!stopRequested.load(std::memory_order_relaxed)) {
      const int result = decodeFrame();
      if (result == -2) break;
      if (result < 0) {
        resetStream();
        continue;
      }
      if (result > 0) {
        if (!publishFrame()) break;
        av_frame_unref(frame);
        continue;
      }

      if (waitForSocket(nativeSocket(socketHandle)) == SOCKET_ERROR && !socketInterrupted()) break;
    }
  } catch (...) {
  }

  if (!stopRequested.load(std::memory_order_relaxed))
    feedStatus.store(VideoFeedStatus::Error, std::memory_order_relaxed);
  shutdownBackend();
}

void VideoUI::clearAccessUnit() {
  accessUnit.clear();
  inFragment = false;
  accessUnitDamaged = false;
  accessUnitHasSps = false;
  accessUnitHasPps = false;
  accessUnitHasIdr = false;
}

void VideoUI::resetStream(bool forgetParameters) {
  clearAccessUnit();
  avcodec_flush_buffers(decoderContext);
  av_frame_unref(frame);
  av_packet_unref(packet);
  decoderSynced = false;
  sequenceStarted = false;
  timestampStarted = false;
  streamStarted = false;
  lastPacketAt = {};
  if (forgetParameters) {
    cachedSps.clear();
    cachedPps.clear();
  }
  feedStatus.store(VideoFeedStatus::WaitingForStream, std::memory_order_relaxed);
}

int VideoUI::receiveAccessUnit() {
  const auto now = std::chrono::steady_clock::now();
  if (now >= nextKeepalive) {
    sendSocket(nativeSocket(socketHandle), "videoClient keepalive", 21);
    nextKeepalive = now + std::chrono::seconds(2);
  }

  uint8_t datagram[65535];
  for (int packetCount = 0; packetCount < 256; ++packetCount) {
    const int size =
        receiveSocket(nativeSocket(socketHandle), datagram, static_cast<int>(sizeof(datagram)));
    if (size < 0) {
      if (socketWouldBlock()) {
        if (lastPacketAt.time_since_epoch().count() && now - lastPacketAt > STREAM_TIMEOUT)
          resetStream();
        return 0;
      }
      if (socketInterrupted()) continue;
      if (socketReset()) {
        resetStream();
        return 0;
      }
      return -2;
    }
    if (size < 12 || datagram[0] >> 6 != 2 || (datagram[1] & 0x7f) != RTP_PAYLOAD_TYPE) continue;

    const uint16_t sequence = readU16(datagram + 2);
    const uint32_t timestamp = readU32(datagram + 4);
    const uint32_t ssrc = readU32(datagram + 8);
    const int32_t timestampDelta = static_cast<int32_t>(timestamp - lastTimestamp);
    if ((streamStarted && ssrc != streamSsrc) ||
        (timestampStarted && timestampDelta < 0 &&
         (timestampDelta < -90000 || (sequenceStarted && sequence != expectedSequence))))
      resetStream(true);
    streamSsrc = ssrc;
    streamStarted = true;
    lastPacketAt = now;

    VideoFeedStatus waiting = VideoFeedStatus::WaitingForStream;
    feedStatus.compare_exchange_strong(waiting, VideoFeedStatus::Synchronizing,
                                       std::memory_order_relaxed);

    size_t headerSize = 12 + (datagram[0] & 0x0f) * 4;
    if (headerSize > static_cast<size_t>(size)) continue;
    if (datagram[0] & 0x10) {
      if (headerSize + 4 > static_cast<size_t>(size)) continue;
      headerSize += 4 + readU16(datagram + headerSize + 2) * 4;
    }

    size_t payloadEnd = size;
    if (datagram[0] & 0x20) {
      const uint8_t padding = datagram[size - 1];
      if (!padding || padding > payloadEnd - headerSize) continue;
      payloadEnd -= padding;
    }
    if (headerSize >= payloadEnd) continue;

    const bool sequenceGap = sequenceStarted && sequence != expectedSequence;
    expectedSequence = static_cast<uint16_t>(sequence + 1);
    sequenceStarted = true;

    const bool continuingAccessUnit = !accessUnit.empty() && timestamp == accessUnitTimestamp;
    if (!accessUnit.empty() && timestamp != accessUnitTimestamp) {
      clearAccessUnit();
      if (decoderSynced) {
        decoderSynced = false;
        avcodec_flush_buffers(decoderContext);
      }
    }
    if (accessUnit.empty()) accessUnitTimestamp = timestamp;
    if (sequenceGap && continuingAccessUnit) accessUnitDamaged = true;

    const uint8_t* payload = datagram + headerSize;
    const size_t payloadSize = payloadEnd - headerSize;
    const uint8_t nalType = payload[0] & 0x1f;
    const auto noteNal = [this](uint8_t type, const uint8_t* nal, size_t nalSize) {
      if (type == 5)
        accessUnitHasIdr = true;
      else if (type == 7) {
        accessUnitHasSps = true;
        cachedSps.assign(nal, nal + nalSize);
      } else if (type == 8) {
        accessUnitHasPps = true;
        cachedPps.assign(nal, nal + nalSize);
      }
    };

    if (nalType >= 1 && nalType <= 23) {
      if (!appendNal(accessUnit, payload, payloadSize)) accessUnitDamaged = true;
      noteNal(nalType, payload, payloadSize);
    } else if (nalType == 24) {
      size_t offset = 1;
      while (offset + 2 <= payloadSize) {
        const size_t nalSize = readU16(payload + offset);
        offset += 2;
        if (!nalSize || offset + nalSize > payloadSize) {
          accessUnitDamaged = true;
          break;
        }
        if (!appendNal(accessUnit, payload + offset, nalSize)) accessUnitDamaged = true;
        noteNal(payload[offset] & 0x1f, payload + offset, nalSize);
        offset += nalSize;
      }
    } else if (nalType == 28 && payloadSize >= 2) {
      const bool start = payload[1] & 0x80;
      const bool end = payload[1] & 0x40;
      if (start) {
        if (inFragment) accessUnitDamaged = true;
        const uint8_t header = (payload[0] & 0xe0) | (payload[1] & 0x1f);
        if (!appendNal(accessUnit, &header, 1)) accessUnitDamaged = true;
        if ((header & 0x1f) == 5) accessUnitHasIdr = true;
        inFragment = true;
      } else if (!inFragment) {
        accessUnitDamaged = true;
      }
      if (inFragment) {
        const size_t fragmentSize = payloadSize - 2;
        if (fragmentSize > MAX_ACCESS_UNIT_SIZE - std::min(accessUnit.size(), MAX_ACCESS_UNIT_SIZE))
          accessUnitDamaged = true;
        else
          accessUnit.insert(accessUnit.end(), payload + 2, payload + payloadSize);
      }
      if (end) inFragment = false;
    } else {
      accessUnitDamaged = true;
    }

    if (!(datagram[1] & 0x80)) continue;

    if (accessUnit.empty() || inFragment || accessUnitDamaged) {
      const bool damaged = inFragment || accessUnitDamaged;
      clearAccessUnit();
      if (damaged && decoderSynced) {
        decoderSynced = false;
        avcodec_flush_buffers(decoderContext);
      }
      continue;
    }

    if (!decoderSynced) {
      if (!accessUnitHasIdr || cachedSps.empty() || cachedPps.empty()) {
        clearAccessUnit();
        continue;
      }
      if (!accessUnitHasSps || !accessUnitHasPps) {
        std::vector<uint8_t> synchronized;
        synchronized.reserve(cachedSps.size() + cachedPps.size() + accessUnit.size() + 8);
        if (!appendNal(synchronized, cachedSps.data(), cachedSps.size()) ||
            !appendNal(synchronized, cachedPps.data(), cachedPps.size()) ||
            accessUnit.size() > MAX_ACCESS_UNIT_SIZE - synchronized.size()) {
          clearAccessUnit();
          continue;
        }
        synchronized.insert(synchronized.end(), accessUnit.begin(), accessUnit.end());
        accessUnit.swap(synchronized);
      }
      avcodec_flush_buffers(decoderContext);
      decoderSynced = true;
    }

    av_packet_unref(packet);
    if (av_new_packet(packet, accessUnit.size()) < 0) return -1;
    std::memcpy(packet->data, accessUnit.data(), accessUnit.size());
    clearAccessUnit();

    if (!timestampStarted) {
      lastTimestamp = timestamp;
      extendedTimestamp = timestamp;
      timestampStarted = true;
    } else {
      extendedTimestamp += static_cast<int32_t>(timestamp - lastTimestamp);
      lastTimestamp = timestamp;
    }
    packet->pts = packet->dts = extendedTimestamp;
    return 1;
  }
  return 0;
}

int VideoUI::decodeFrame() {
  for (;;) {
    const int result = avcodec_receive_frame(decoderContext, frame);
    if (result == 0) return 1;
    if (result == AVERROR_INVALIDDATA) continue;
    if (result != AVERROR(EAGAIN)) return -1;

    const int received = receiveAccessUnit();
    if (received <= 0) return received;
    const int sent = avcodec_send_packet(decoderContext, packet);
    av_packet_unref(packet);
    if (sent == AVERROR_INVALIDDATA) continue;
    if (sent < 0 && sent != AVERROR(EAGAIN)) return -1;
  }
}

bool VideoUI::publishFrame() {
  if (framePending.load(std::memory_order_acquire)) return true;
  if (frame->width <= 0 || frame->height <= 0 || frame->width > MAX_VIDEO_DIMENSION ||
      frame->height > MAX_VIDEO_DIMENSION)
    return false;
  const auto decodedFormat = static_cast<AVPixelFormat>(frame->format);
  const AVPixelFormat sourceFormat = normalizePixelFormat(decodedFormat);
  scaler =
      sws_getCachedContext(scaler, frame->width, frame->height, sourceFormat, frame->width,
                           frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (!scaler) return false;

  const int* coefficients = sws_getCoefficients(swsColorSpace(frame->colorspace));
  const int sourceRange = decodedFormat != sourceFormat || frame->color_range == AVCOL_RANGE_JPEG;
  if (sws_setColorspaceDetails(scaler, coefficients, sourceRange, coefficients, 1, 0, 1 << 16,
                               1 << 16) < 0)
    return false;

  const size_t pitch = static_cast<size_t>(frame->width) * 4;
  const size_t frameSize = pitch * static_cast<size_t>(frame->height);
  if (frameSize > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
  if (!frameBuffersInitialized) {
    for (auto& buffer : frameBuffers) buffer.pixels.resize(frameSize);
    frameBuffersInitialized = true;
  }
  VideoFrameBuffer& output = frameBuffers[backendFrame];
  output.pixels.resize(frameSize);
  output.width = frame->width;
  output.height = frame->height;
  uint8_t* pixels[] = {output.pixels.data()};
  const int pitches[] = {static_cast<int>(pitch)};
  sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, pixels, pitches);

  backendFrame = middleFrame.exchange(backendFrame, std::memory_order_acq_rel);
  framePending.store(true, std::memory_order_release);
  return true;
}

bool VideoUI::presentFrame() {
  if (!framePending.exchange(false, std::memory_order_acquire)) return true;
  presentationFrame = middleFrame.exchange(presentationFrame, std::memory_order_acq_rel);
  const VideoFrameBuffer& input = frameBuffers[presentationFrame];
  if (input.width <= 0 || input.height <= 0) return false;
  const size_t inputSize = static_cast<size_t>(input.width) * static_cast<size_t>(input.height) * 4;
  if (input.pixels.size() != inputSize) return false;

  if (videoTexture.Status == ImTextureStatus_Destroyed) {
    videoTexture.Create(ImTextureFormat_RGBA32, input.width, input.height);
    ImGui::RegisterUserTexture(&videoTexture);
  } else if (videoTexture.Width != input.width || videoTexture.Height != input.height) {
    feedStatus.store(VideoFeedStatus::Error, std::memory_order_relaxed);
    return false;
  }

  if (!videoTexture.Pixels || static_cast<size_t>(videoTexture.GetSizeInBytes()) != inputSize)
    return false;
  std::memcpy(videoTexture.Pixels, input.pixels.data(), inputSize);
  if (videoTexture.Status == ImTextureStatus_OK) {
    const ImTextureRect update{0, 0, static_cast<unsigned short>(videoTexture.Width),
                               static_cast<unsigned short>(videoTexture.Height)};
    videoTexture.UpdateRect = videoTexture.UsedRect = update;
    videoTexture.Updates.resize(0);
    videoTexture.Updates.push_back(update);
    videoTexture.SetStatus(ImTextureStatus_WantUpdates);
  }
  feedStatus.store(VideoFeedStatus::Streaming, std::memory_order_relaxed);
  return true;
}

static const char* videoStatusText(VideoFeedStatus status) {
  switch (status) {
    case VideoFeedStatus::Connecting:
      return "Connecting to video server...";
    case VideoFeedStatus::WaitingForStream:
      return "Waiting for video stream...";
    case VideoFeedStatus::Synchronizing:
      return "Video loading...";
    case VideoFeedStatus::Error:
      return "Video feed unavailable";
    case VideoFeedStatus::Streaming:
      return nullptr;
  }
  return nullptr;
}

float VideoUI::rotatedAspect() const {
  return videoTexture.Width > 0 && videoTexture.Height > 0
             ? static_cast<float>(videoTexture.Height) / videoTexture.Width
             : 9.0f / 16.0f;
}

void VideoUI::drawContent(ImVec2 size) {
  init();
  if (!presentFrame()) feedStatus.store(VideoFeedStatus::Error, std::memory_order_relaxed);
  size = {std::max(size.x, 1.0f), std::max(size.y, 1.0f)};
  ImGui::Dummy(size);
  const ImVec2 paneMin = ImGui::GetItemRectMin();
  const ImVec2 paneMax = ImGui::GetItemRectMax();
  ImVec2 imageMin = paneMin;
  ImVec2 imageMax = paneMax;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(paneMin, paneMax, true);
  draw->AddRectFilled(paneMin, paneMax, ImGui::GetColorU32(ImGuiCol_FrameBg));
  if (videoTexture.Status != ImTextureStatus_Destroyed && videoTexture.Width > 0 &&
      videoTexture.Height > 0) {
    const float aspect = rotatedAspect();
    ImVec2 drawSize{size.x, size.x / aspect};
    if (drawSize.y > size.y) drawSize = {size.y * aspect, size.y};
    imageMin = {(paneMin.x + paneMax.x - drawSize.x) * 0.5f,
                (paneMin.y + paneMax.y - drawSize.y) * 0.5f};
    imageMax = {imageMin.x + drawSize.x, imageMin.y + drawSize.y};
    draw->AddImageQuad(videoTexture.GetTexRef(), imageMin, {imageMax.x, imageMin.y}, imageMax,
                       {imageMin.x, imageMax.y}, {0, 1}, {0, 0}, {1, 0}, {1, 1});
  }
  if (const char* text = videoStatusText(feedStatus.load(std::memory_order_relaxed))) {
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    draw->AddText({(imageMin.x + imageMax.x - textSize.x) * 0.5f,
                   (imageMin.y + imageMax.y - textSize.y) * 0.5f},
                  IM_COL32(220, 220, 220, 255), text);
  }
  draw->PopClipRect();
}

void VideoUI::videoController(ImGuiWindowFlags flags) {
  const bool visible = ImGui::Begin("Video Controller", nullptr, flags);
  if (visible) drawContent(ImGui::GetContentRegionAvail());
  ImGui::End();
}
