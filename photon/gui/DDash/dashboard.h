#pragma once

#include "imgui.h"
#include "state.h"

namespace ui {

/**
 * Render the complete vehicle dashboard
 * @param state Application state (read/write)
 */
void RenderDashboard(AppState& state, ImGuiWindowFlags flags = 0);

}  // namespace ui
