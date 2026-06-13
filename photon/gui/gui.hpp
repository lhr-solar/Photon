#pragma once
#include "../gpu/gpu.hpp"
#include "../gpu/shader.hpp"
#include "../network/network.hpp"
#include "../parse/arena.hpp"
#include "../parse/spmc.hpp"
#include "canvas.hpp"
#include "config.hpp"
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

  void shaderTest(ImGuiWindowFlags flags);
  void testFunc(ImGuiWindowFlags flags);
  void plotTest(ImGuiWindowFlags flags);
  void networkPage(ImGuiWindowFlags flags);

  GPU* gpu;
  Arena* arena;
  Network* network;

  TitleBar titleBar{};
  Sidebar sideBar{};
  Tabs tabs{};
  Canvas canvas{};
  Shader testShader{};
  GuiSettings settings{};
  GuiFlags flags{};
  std::vector<const char*> networkOptions = {"DAQ Server", "TCP", "UDP", "UART",
                                             "PCAN",       "BLE", "WLAN"};
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
