#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

struct GPUMetalState;
struct TitleBar;

struct GPU {
  void init();
  void imguiBackend(TitleBar* titleBar);
  void imguiPresentation(const uint32_t imgIdx);
  void startFrame(uint32_t& imgIdx);
  void startCommands();
  void endCommands();
  void submitFrame(const uint32_t imgIdx);
  void resizeWindow();
  void destroy();
  SDL_Window* createWindow();
  void enableCustomTitlebar(TitleBar* titleBarState);
  void updateImguiDisplayMetrics();
  void queryWindowPixelSize(uint32_t& outWidth, uint32_t& outHeight) const;

  uint32_t width = 1280;
  uint32_t height = 720;
  bool resizePending = false;
  SDL_Window* window = nullptr;
  std::vector<uint32_t> swapchainImages{};
  uint32_t frameIndex = 0;

 private:
  GPUMetalState* metal = nullptr;
  uint32_t frameInFlight = 0;
  uint64_t frameSerial = 0;
  uint64_t pendingSignalValue = 0;
  bool frameActive = false;
};
