#pragma once
#include <array>

#include "../gpu/gpu.hpp"
#if !defined(APPLE) && !defined(__APPLE__)
#define PHOTON_GUI_RENDER_ITEMS 1
#include "../gpu/shader.hpp"
#include "scene.hpp"
#else
#define PHOTON_GUI_RENDER_ITEMS 0
#endif
#include "../network/network.hpp"
#include "../parse/arena.hpp"
#include "../parse/spmc.hpp"
#include "canvas.hpp"
#include "config.hpp"
#include "exporter.hpp"
#include "plots.hpp"
#include "sideBar.hpp"
#include "tabs.hpp"
#include "titlebar.hpp"
#include "updater.hpp"
#include "video_ui.h"

struct GUI {
  void init(GPU& gpu, Arena& arena, Network& network);
  void setTabs();
  void destroy();
  void setFont();
  void buildUI();
  void render();
  void settingsUI();
  void updateUI();
  void exportUI();

  void shaderTest(ImGuiWindowFlags flags);
  void testFunc(ImGuiWindowFlags flags);
  void plotTest(ImGuiWindowFlags flags);
  void networkPage(ImGuiWindowFlags flags);
  void connectDaqServer();
  void updateNetworkStatus();
  void drawButtonShaderOverlay(ImVec2 buttonMin, ImVec2 buttonMax);
  void liveView(ImGuiWindowFlags flags);
  void dynamicsView(ImGuiWindowFlags flags);
  void batteryView(ImGuiWindowFlags flags);

  GPU* gpu;
  Arena* arena;
  Network* network;

  TitleBar titleBar{};
  Sidebar sideBar{};
  Tabs tabs{};
  Canvas canvas{};
#if PHOTON_GUI_RENDER_ITEMS
  Shader testShader{};
  Shader buttonShader{};
#endif
  GuiSettings settings{};
  GuiFlags flags{};
  bool updateAvailable = false;
  Updater updater;
  Plots plots;
  VideoUI videoUi;
  Exporter exporter;
#if PHOTON_GUI_RENDER_ITEMS
  Scene scene;
  Scene dynamicsScene;
  int dynamicsObjectIndex{-1};
  Scene batteryScene;
  int batteryObjectIndex{-1};
  struct DynamicsTelemetry {
    float steeringDegrees{};
    float frontLeftRpm{};
    float frontRightRpm{};
    float rearRpm{};
    std::array<std::array<float, 3>, 3> acceleration{};
    std::array<std::array<float, 3>, 3> angularVelocity{};
    std::array<std::array<float, 3>, 3> accelerationReference{};
    std::array<bool, 3> accelerationReferenceValid{};
    std::array<float, 3> suspensionRaw{};
    std::array<float, 3> suspensionReference{};
    std::array<bool, 3> suspensionReferenceValid{};
    double lastCursor{-1.0};
    float jiggleTime{};
    bool jiggle{};
    bool hasSteering{};
    bool hasWheelSpeed{};
    bool hasSuspension{};
    std::array<bool, 3> hasAcceleration{};
    std::array<bool, 3> hasAngularVelocity{};
    bool hasImu{};
  } dynamicsTelemetry;
  struct MapTracker {
    Position position{};
    double time{};
    double gpsTime{};
    float speed{};
    float heading{};
    bool valid{};
  } mapTracker;
#endif
};

/* forward function handles */
void ImAnimDemoWindow();
void ImAnimDocWindow();

/* Toggles flag if key is released. If flag is true, executes function */
template <typename F, typename... Args>
decltype(auto) ifKey(ImGuiKey key, bool& flag, F&& func, Args&&... args) {
  if (ImGui::IsKeyReleased(key)) flag = !flag;
  if (flag) std::forward<F>(func)(std::forward<Args>(args)...);
}

/* aligns the given text to rhs of the window */
template <typename... Args>
void TextRightAligned(const char* fmt, Args... args) {
  char buf[128] = {};
  snprintf(buf, sizeof(buf), fmt, args...);
  ImVec2 dims = ImGui::CalcTextSize(buf);
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - dims.x);
  ImGui::TextUnformatted(buf);
}

#if PHOTON_GUI_RENDER_ITEMS
inline VkExtent2D quantizeContentExtent(ImVec2 contentSize, VkExtent2D fallback) {
  if (contentSize.x <= 1.0f || contentSize.y <= 1.0f) return fallback;
  const uint32_t width = std::max(1u, static_cast<uint32_t>(std::lround(contentSize.x)));
  const uint32_t height = std::max(1u, static_cast<uint32_t>(std::lround(contentSize.y)));
  return {width, height};
}
#endif
