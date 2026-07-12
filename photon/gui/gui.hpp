#pragma once
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
  void drawButtonShaderOverlay(ImVec2 buttonMin, ImVec2 buttonMax);
  void carMap(ImGuiWindowFlags flags);

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
  Exporter exporter;
#if PHOTON_GUI_RENDER_ITEMS
  Scene scene;
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
