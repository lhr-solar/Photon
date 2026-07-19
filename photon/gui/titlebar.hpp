#pragma once
#include <SDL3/SDL_dialog.h>

#include <array>
#include <string>

#include "imgui.h"

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
  std::string connectionProtocol = "Offline";
  bool connectionActive = false;
  bool connectionConnected = false;
  bool connectionFailed = false;
  int interactiveRectCount = 0;
  WindowAction pendingAction = WindowAction::None;
  static constexpr int buttonCount = 4;
  std::array<SDL_Rect, buttonCount> interactiveRects{};
  void clearInteract();
  void addInteract(const ImVec2& min, const ImVec2& max);
  bool isInteract(int x, int y) const;
  void draw();
};
