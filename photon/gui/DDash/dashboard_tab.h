#pragma once

#include "imgui.h"
#include "state.h"

namespace ui {

struct DashboardTab {
  AppState state = CreateDefaultState();

  void draw(ImGuiWindowFlags flags);
};

DashboardTab& dashboardTab();

}  // namespace ui
