#pragma once
#include "imgui.h"
#include "sideBar.hpp"
#include "tabs.hpp"
#include "titlebar.hpp"

struct Canvas {
  float width = 0.0f;

  void draw(TitleBar& titleBar, Sidebar& sideBar, Tabs& tabs) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 viewportSize = viewport->Size;
    const float titleBarHeight = titleBar.enabled ? titleBar.height : 0.0f;
    ImVec2 canvasSize = {viewportSize.x - sideBar.width, viewportSize.y - titleBarHeight};
    ImVec2 canvasPos = {sideBar.width, titleBarHeight};
    width = canvasSize.x;
    ImGui::SetNextWindowSize(canvasSize);
    ImGui::SetNextWindowPos(canvasPos);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking;
    if (!tabs.list.empty() && tabs.index < tabs.list.size()) tabs.list[tabs.index].draw(flags);
  }
};
