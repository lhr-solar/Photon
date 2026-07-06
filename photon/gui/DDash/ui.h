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

inline void InitUI(const char* fontPath = nullptr,
                   const char* monoFontPath = nullptr,
                   const char* iconFontPath = nullptr) {
    ApplyTheme();
    LoadFonts(fontPath, monoFontPath, iconFontPath);
}

inline void RenderUI(AppState& state) {
    RenderDashboard(state);
}

} // namespace ui
