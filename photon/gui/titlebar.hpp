#pragma once
#include <SDL3/SDL_dialog.h>

#include <array>
#include <string>

#include "imgui.h"

// Forward declare to avoid pulling in the full headers
namespace io {
class Pre_Fault_Recorder;
class Replay_Controller;
}

enum class WindowAction {
  None,
  Close,
  Minimize,
  ToggleMaximize,
};

struct TitleBar {
  SDL_Window* window;
  float height = 30;
  bool enabled = true;
  bool showSidebar = true;
  std::string activePage = "Navigation";
  int interactiveRectCount = 0;
  WindowAction pendingAction = WindowAction::None;
  static constexpr int buttonCount = 5;  // +1 for recorder snapshot button
  std::array<SDL_Rect, buttonCount> interactiveRects{};

  // Set by GUI before draw() — null if unavailable
  io::Pre_Fault_Recorder*  recorder{nullptr};
  io::Replay_Controller*   replayController{nullptr};

  void clearInteract();
  void addInteract(const ImVec2& min, const ImVec2& max);
  bool isInteract(int x, int y) const;
  void draw();
};
