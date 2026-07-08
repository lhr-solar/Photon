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
    ImVec2 canvasSize = {viewportSize.x - sideBar.width, viewportSize.y - titleBar.height};
    ImVec2 canvasPos = {sideBar.width, titleBar.height};
    width = canvasSize.x;
    ImGui::SetNextWindowSize(canvasSize);
    ImGui::SetNextWindowPos(canvasPos);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (!tabs.list.empty() && tabs.index < tabs.list.size()) tabs.list[tabs.index].draw(flags);
    ImGui::PopStyleVar(3);
  }
};
