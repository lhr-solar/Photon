#include "dashboard_tab.h"

#include "../../parse/arena.hpp"
#include "arena_bridge.hpp"
#include "dashboard.h"

namespace ui {

void DashboardTab::draw(ImGuiWindowFlags flags) {
  if (arena) {
    ArenaReadScope read(*arena);
    UpdateDashboardState(*arena, state);
  }
  RenderDashboard(state, flags);
}

DashboardTab& dashboardTab() {
  static DashboardTab tab;
  return tab;
}

}  // namespace ui
