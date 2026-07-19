#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "imgui.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

struct VideoUI {
  ImTextureData videoTexture;
  AVCodecContext* decoderContext = nullptr;
  AVFrame* frame = nullptr;
  AVFrame* displayFrame = nullptr;
  AVPacket* packet = nullptr;
  SwsContext* scaler = nullptr;
  std::vector<uint8_t> accessUnit;
  std::vector<uint8_t> cachedSps;
  std::vector<uint8_t> cachedPps;
  std::chrono::steady_clock::time_point nextKeepalive;
  int socketFd = -1;
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
  bool initialized = false;

  ~VideoUI();
  bool init();
  void clearAccessUnit();
  int receiveAccessUnit();
  int decodeFrame();
  bool nextFrame();
  void videoController();
};
