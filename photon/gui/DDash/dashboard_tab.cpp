#include "dashboard_tab.h"

#include "dashboard.h"

namespace ui {

void DashboardTab::draw(ImGuiWindowFlags flags) { RenderDashboard(state, flags); }

DashboardTab& dashboardTab() {
  static DashboardTab tab;
  return tab;
}

}  // namespace ui
