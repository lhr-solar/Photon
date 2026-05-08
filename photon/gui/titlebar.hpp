#pragma once
#include <SDL3/SDL_dialog.h>

#include <array>

#include "imgui.h"

enum class WindowAction {
  None,
  Close,
  Minimize,
  ToggleMaximize,
};

struct TitleBar {
  SDL_Window* window;
  float height = 40;
  bool enabled = true;
  bool showSidebar = true;
  int interactiveRectCount = 0;
  WindowAction pendingAction = WindowAction::None;
  static constexpr int buttonCount = 4;
  std::array<SDL_Rect, buttonCount> interactiveRects{};
  void clearInteract();
  void addInteract(const ImVec2& min, const ImVec2& max);
  bool isInteract(int x, int y) const;
  void draw();
};
