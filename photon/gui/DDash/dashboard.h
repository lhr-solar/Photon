#pragma once

#include "state.h"

namespace ui {

/**
 * Render the complete vehicle dashboard
 * @param state Application state (read/write)
 */
void RenderDashboard(AppState& state);

/**
 * Render header with branding, heartbeat, and gear selector
 */
void RenderHeader(AppState& state);

/**
 * Render the speed gauge with circular arc and gear badge
 */
void RenderSpeedGauge(const AppState& state);

/**
 * Render stacked camera feeds (rear view + side camera)
 */
void RenderCameraFeeds(AppState& state);

/**
 * Render battery status panel (main + 12V aux)
 */
void RenderBatteryPanel(const AppState& state);

/**
 * Render system status (contactors, brake, cruise, turn signals)
 */
void RenderSystemStatus(AppState& state);

/**
 * Render horizontal fault ticker at bottom
 */
void RenderFaultTicker(AppState& state);

// Legacy functions (kept for compatibility, now no-ops)
void RenderGearIndicator(AppState& state);
void RenderCruiseControl(AppState& state);
void RenderFaultPanel(AppState& state);
void RenderCameraFeed(const char* label, const char* type, bool isActive, void* texture = nullptr);
bool RenderTurnIndicator(bool isLeft, bool active);

} // namespace ui

