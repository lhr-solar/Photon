#pragma once

#include "imgui.h"
#include "state.h"

struct Arena;

namespace ui {

struct DashboardTab {
  AppState state = CreateDefaultState();
  Arena* arena{};

  void init(Arena* arenaTarget) { arena = arenaTarget; }
  void draw(ImGuiWindowFlags flags);
};

DashboardTab& dashboardTab();

}  // namespace ui
