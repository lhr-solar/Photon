#include "video_ui.h"

#include "imgui_internal.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

static constexpr char SERVER_IP[] = "3.141.38.115";
static constexpr uint16_t SERVER_PORT = 6600;
static constexpr uint8_t RTP_PAYLOAD_TYPE = 96;
static constexpr uint8_t START_CODE[] = {0, 0, 0, 1};

static uint16_t readU16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0] << 8 | data[1]);
}

static uint32_t readU32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) << 24 | static_cast<uint32_t>(data[1]) << 16 |
         static_cast<uint32_t>(data[2]) << 8 | data[3];
}

static void appendNal(std::vector<uint8_t>& accessUnit, const uint8_t* data, size_t size) {
  accessUnit.insert(accessUnit.end(), std::begin(START_CODE), std::end(START_CODE));
  accessUnit.insert(accessUnit.end(), data, data + size);
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

VideoUI::~VideoUI() {
  if (socketFd >= 0) {
    send(socketFd, "bye", 3, 0);
    close(socketFd);
  }
  sws_freeContext(scaler);
  av_frame_free(&frame);
  av_frame_free(&displayFrame);
  av_packet_free(&packet);
  avcodec_free_context(&decoderContext);
}

bool VideoUI::init() {
  av_log_set_level(AV_LOG_QUIET);

  const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!decoder) return false;
  decoderContext = avcodec_alloc_context3(decoder);
  if (decoderContext) decoderContext->pkt_timebase = {1, 90000};
  if (!decoderContext || avcodec_open2(decoderContext, decoder, nullptr) < 0) return false;

  frame = av_frame_alloc();
  displayFrame = av_frame_alloc();
  packet = av_packet_alloc();
  if (!frame || !displayFrame || !packet) return false;

  socketFd = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in server{};
  server.sin_family = AF_INET;
  server.sin_port = htons(SERVER_PORT);
  if (socketFd < 0 || inet_pton(AF_INET, SERVER_IP, &server.sin_addr) != 1 ||
      connect(socketFd, reinterpret_cast<sockaddr*>(&server), sizeof(server)) < 0 ||
      fcntl(socketFd, F_SETFL, fcntl(socketFd, F_GETFL) | O_NONBLOCK) < 0)
    return false;

  accessUnit.reserve(1024 * 1024);
  send(socketFd, "videoClient keepalive", 21, 0);
  nextKeepalive = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  return initialized = true;
}

void VideoUI::clearAccessUnit() {
  accessUnit.clear();
  inFragment = false;
  accessUnitDamaged = false;
  accessUnitHasSps = false;
  accessUnitHasPps = false;
  accessUnitHasIdr = false;
}

int VideoUI::receiveAccessUnit() {
  const auto now = std::chrono::steady_clock::now();
  if (now >= nextKeepalive) {
    send(socketFd, "videoClient keepalive", 21, 0);
    nextKeepalive = now + std::chrono::seconds(2);
  }

  uint8_t datagram[65535];
  for (int packetCount = 0; packetCount < 256; ++packetCount) {
    const ssize_t size = recv(socketFd, datagram, sizeof(datagram), 0);
    if (size < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
      if (errno == EINTR) continue;
      return -1;
    }
    if (size < 12 || datagram[0] >> 6 != 2 || (datagram[1] & 0x7f) != RTP_PAYLOAD_TYPE) continue;

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

    const uint16_t sequence = readU16(datagram + 2);
    const bool sequenceGap = sequenceStarted && sequence != expectedSequence;
    expectedSequence = static_cast<uint16_t>(sequence + 1);
    sequenceStarted = true;

    const uint32_t timestamp = readU32(datagram + 4);
    if (!accessUnit.empty() && timestamp != accessUnitTimestamp) {
      clearAccessUnit();
    }
    if (accessUnit.empty()) accessUnitTimestamp = timestamp;
    if (sequenceGap) accessUnitDamaged = true;

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
      appendNal(accessUnit, payload, payloadSize);
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
        appendNal(accessUnit, payload + offset, nalSize);
        noteNal(payload[offset] & 0x1f, payload + offset, nalSize);
        offset += nalSize;
      }
    } else if (nalType == 28 && payloadSize >= 2) {
      const bool start = payload[1] & 0x80;
      const bool end = payload[1] & 0x40;
      if (start) {
        if (inFragment) accessUnitDamaged = true;
        const uint8_t header = (payload[0] & 0xe0) | (payload[1] & 0x1f);
        appendNal(accessUnit, &header, 1);
        if ((header & 0x1f) == 5) accessUnitHasIdr = true;
        inFragment = true;
      } else if (!inFragment) {
        accessUnitDamaged = true;
      }
      if (inFragment) {
        accessUnit.insert(accessUnit.end(), payload + 2, payload + payloadSize);
      }
      if (end) inFragment = false;
    } else {
      accessUnitDamaged = true;
    }

    if (!(datagram[1] & 0x80)) continue;

    if (accessUnit.empty() || inFragment || accessUnitDamaged) {
      clearAccessUnit();
      if (decoderSynced) {
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
        appendNal(synchronized, cachedSps.data(), cachedSps.size());
        appendNal(synchronized, cachedPps.data(), cachedPps.size());
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
    if (result != AVERROR(EAGAIN)) return -1;

    const int received = receiveAccessUnit();
    if (received <= 0) return received;
    const int sent = avcodec_send_packet(decoderContext, packet);
    av_packet_unref(packet);
    if (sent < 0 && sent != AVERROR(EAGAIN)) return -1;
  }
}

bool VideoUI::nextFrame() {
  bool decoded = false;
  for (int frameCount = 0; frameCount < 4; ++frameCount) {
    const int result = decodeFrame();
    if (result < 0) return false;
    if (result == 0) break;
    av_frame_unref(displayFrame);
    av_frame_move_ref(displayFrame, frame);
    decoded = true;
  }
  if (!decoded) return true;

  if (videoTexture.Status == ImTextureStatus_Destroyed) {
    videoTexture.Create(ImTextureFormat_RGBA32, displayFrame->width, displayFrame->height);
    ImGui::RegisterUserTexture(&videoTexture);
  } else if (videoTexture.Width != displayFrame->width ||
             videoTexture.Height != displayFrame->height) {
    return false;
  }

  const auto decodedFormat = static_cast<AVPixelFormat>(displayFrame->format);
  const AVPixelFormat sourceFormat = normalizePixelFormat(decodedFormat);
  scaler = sws_getCachedContext(scaler, displayFrame->width, displayFrame->height, sourceFormat,
                                displayFrame->width, displayFrame->height, AV_PIX_FMT_RGBA,
                                SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (!scaler) return false;

  const int* coefficients = sws_getCoefficients(swsColorSpace(displayFrame->colorspace));
  const int sourceRange =
      decodedFormat != sourceFormat || displayFrame->color_range == AVCOL_RANGE_JPEG;
  if (sws_setColorspaceDetails(scaler, coefficients, sourceRange, coefficients, 1, 0, 1 << 16,
                               1 << 16) < 0)
    return false;

  uint8_t* pixels[] = {videoTexture.Pixels};
  int pitches[] = {videoTexture.GetPitch()};
  sws_scale(scaler, displayFrame->data, displayFrame->linesize, 0, displayFrame->height, pixels,
            pitches);
  if (videoTexture.Status == ImTextureStatus_OK) {
    const ImTextureRect update{0, 0, static_cast<unsigned short>(videoTexture.Width),
                               static_cast<unsigned short>(videoTexture.Height)};
    videoTexture.UpdateRect = videoTexture.UsedRect = update;
    videoTexture.Updates.resize(0);
    videoTexture.Updates.push_back(update);
    videoTexture.SetStatus(ImTextureStatus_WantUpdates);
  }
  return true;
}

void VideoUI::videoController() {
  if (initialized) nextFrame();
  ImGui::SetNextWindowSize(ImVec2(640.0f, 400.0f), ImGuiCond_FirstUseEver);
  const bool visible = ImGui::Begin("Video Controller");
  if (visible) {
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = size.x > 1.0f ? size.x : 1.0f;
    size.y = size.y > 1.0f ? size.y : 1.0f;
    if (videoTexture.Status != ImTextureStatus_Destroyed)
      ImGui::Image(videoTexture.GetTexRef(), size);
    else
      ImGui::Dummy(size);
  }
  ImGui::End();
}
