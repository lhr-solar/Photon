#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "imgui.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

enum class VideoFeedStatus : uint8_t {
  Connecting,
  WaitingForStream,
  Synchronizing,
  Streaming,
  Error,
};

struct VideoFrameBuffer {
  std::vector<uint8_t> pixels;
  int width = 0;
  int height = 0;
};

struct VideoUI {
  ImTextureData videoTexture;
  AVCodecContext* decoderContext = nullptr;
  AVFrame* frame = nullptr;
  AVPacket* packet = nullptr;
  SwsContext* scaler = nullptr;
  std::vector<uint8_t> accessUnit;
  std::vector<uint8_t> cachedSps;
  std::vector<uint8_t> cachedPps;
  std::array<VideoFrameBuffer, 3> frameBuffers;
  std::thread backendThread;
  std::chrono::steady_clock::time_point nextKeepalive;
  std::chrono::steady_clock::time_point lastPacketAt;
  std::atomic<bool> stopRequested = false;
  std::atomic<bool> framePending = false;
  std::atomic<VideoFeedStatus> feedStatus = VideoFeedStatus::Connecting;
  std::atomic<uint8_t> middleFrame = 1;
  uintptr_t socketHandle = UINTPTR_MAX;
  uint8_t backendFrame = 2;
  uint8_t presentationFrame = 0;
  uint32_t accessUnitTimestamp = 0;
  uint32_t lastTimestamp = 0;
  uint32_t streamSsrc = 0;
  int64_t extendedTimestamp = 0;
  uint16_t expectedSequence = 0;
  bool timestampStarted = false;
  bool sequenceStarted = false;
  bool inFragment = false;
  bool accessUnitDamaged = false;
  bool accessUnitHasSps = false;
  bool accessUnitHasPps = false;
  bool accessUnitHasIdr = false;
  bool decoderSynced = false;
  bool streamStarted = false;
  bool frameBuffersInitialized = false;
  bool socketSubsystemInitialized = false;

  ~VideoUI();
  bool init();
  void stop();
  bool initBackend();
  void shutdownBackend();
  void backendLoop();
  void clearAccessUnit();
  void resetStream(bool forgetParameters = false);
  int receiveAccessUnit();
  int decodeFrame();
  bool publishFrame();
  bool presentFrame();
  void drawContent(ImVec2 size);
  void videoController(ImGuiWindowFlags flags);
};
