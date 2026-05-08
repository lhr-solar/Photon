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
    constexpr float splitterWidth = 3.0f;
    ImVec2 canvasSize = {viewportSize.x - sideBar.width - splitterWidth,
                         viewportSize.y - titleBar.height};
    ImVec2 canvasPos = {sideBar.width + splitterWidth, titleBar.height};
    width = canvasSize.x;
    ImGui::SetNextWindowSize(canvasSize);
    ImGui::SetNextWindowPos(canvasPos);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration;
    if (!tabs.list.empty() && tabs.index < tabs.list.size()) tabs.list[tabs.index].draw(flags);
    ImGui::PopStyleColor();
  }
};
