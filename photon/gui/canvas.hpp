#pragma once
#include "imgui.h"
#include "sideBar.hpp"
#include "tabs.hpp"
#include "titlebar.hpp"

struct Canvas {
  float width = 0.0f;
  ImVec2 pos{};
  ImVec2 size{};

  void draw(TitleBar& titleBar, Sidebar& sideBar, Tabs& tabs, float bottomInset = 0.0f) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    pos = {viewport->Pos.x + sideBar.width, viewport->Pos.y + titleBar.height};
    size = {viewport->Size.x - sideBar.width, viewport->Size.y - titleBar.height - bottomInset};
    width = size.x;
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (!tabs.list.empty() && tabs.index < tabs.list.size()) tabs.list[tabs.index].draw(flags);
    ImGui::PopStyleVar(3);
  }
};
