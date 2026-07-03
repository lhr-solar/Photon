#pragma once
#include "../gpu/gpu.hpp"
#include "../gpu/shader.hpp"
#include "../network/network.hpp"
#include "../parse/arena.hpp"
#include "../parse/spmc.hpp"
#include "canvas.hpp"
#include "config.hpp"
#include "plots.hpp"
#include "sideBar.hpp"
#include "tabs.hpp"
#include "titlebar.hpp"

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

  void genericPlot(uint32_t id, uint32_t signal, ImVec2 size);
  void shaderTest(ImGuiWindowFlags flags);
  void testFunc(ImGuiWindowFlags flags);
  void plotTest(ImGuiWindowFlags flags);
  void networkPage(ImGuiWindowFlags flags);
  void drawButtonShaderOverlay(ImVec2 buttonMin, ImVec2 buttonMax);

  GPU* gpu;
  Arena* arena;
  Network* network;

  TitleBar titleBar{};
  Sidebar sideBar{};
  Tabs tabs{};
  Canvas canvas{};
  Shader testShader{};
  Shader buttonShader{};
  GuiSettings settings{};
  GuiFlags flags{};
  bool updateAvailable = true;
  std::vector<Plots> plots;
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
