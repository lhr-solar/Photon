#pragma once

/**
 * Vehicle Dashboard ImGui UI
 * 
 * Main integration header - include this in your render loop
 */

#include "state.h"
#include "theme.h"
#include "widgets.h"
#include "dashboard.h"

namespace ui {

/**
 * Initialize the UI system
 * Call once after creating ImGui context
 * 
 * @param fontPath Optional path to custom UI font
 * @param monoFontPath Optional path to monospace font
 * @param iconFontPath Optional path to icon font (Font Awesome, etc.)
 */
inline void InitUI(const char* fontPath = nullptr, 
                   const char* monoFontPath = nullptr,
                   const char* iconFontPath = nullptr) {
    ApplyTheme();
    LoadFonts(fontPath, monoFontPath, iconFontPath);
}

/**
 * Render the complete UI
 * Call this from your main render loop
 * 
 * @param state Application state (read/write access for interaction)
 * 
 * Example usage:
 * @code
 *   // In your render loop:
 *   ImGui_ImplXXX_NewFrame();
 *   ImGui::NewFrame();
 *   
 *   ui::RenderUI(appState);
 *   
 *   ImGui::Render();
 *   ImGui_ImplXXX_RenderDrawData(ImGui::GetDrawData());
 * @endcode
 */
inline void RenderUI(AppState& state) {
    RenderDashboard(state);
}

/**
 * Call this at regular intervals to update state
 * 
 * @param state Application state to update
 * @param deltaTime Time since last update in seconds
 */
inline void UpdateSimulation(AppState& state, float deltaTime) {
    state.heartbeat = static_cast<uint8_t>((state.heartbeat + 1) % 256);
}

} // namespace ui
