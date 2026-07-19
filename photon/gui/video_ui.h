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
  std::atomic<bool> stopRequested = false;
  std::atomic<bool> framePending = false;
  std::atomic<uint8_t> middleFrame = 1;
  int socketFd = -1;
  uint8_t backendFrame = 2;
  uint8_t presentationFrame = 0;
  uint32_t accessUnitTimestamp = 0;
  uint32_t lastTimestamp = 0;
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
  bool frameBuffersInitialized = false;

  ~VideoUI();
  bool init();
  void stop();
  bool initBackend();
  void shutdownBackend();
  void backendLoop();
  void clearAccessUnit();
  int receiveAccessUnit();
  int decodeFrame();
  bool publishFrame();
  bool presentFrame();
  void videoController();
};
