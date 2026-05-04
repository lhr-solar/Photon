#include "gui.hpp"
#include "sideBar.hpp"
#include "titlebar.hpp"
#include "imgui.h"
#include "im_anim.h"
#include "tabs.hpp"
#include "style.hpp"

void Sidebar::draw(GUI &gui){
    auto& titleBar = gui.titleBar;
    auto& tabs = gui.tabs;
    ImVec2 winSize = ImGui::GetMainViewport()->Size;
    storedWidth = std::clamp(storedWidth, winSize.x * 0.05f, winSize.x * 0.5f);
    width = iam_tween_float(
        ImGui::GetID("sidebar"), 0, titleBar.showSidebar ? storedWidth : 0.0f, 0.10f,
        iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, ImGui::GetIO().DeltaTime
    );
    if(width <= 0.5f && !titleBar.showSidebar) { width = 0.0f; return; }
    ImVec2 pos = {0, (float)titleBar.height};
    ImVec2 dim = {width, winSize.y - (float)titleBar.height};
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(dim);
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.0f, 0.0f));
    if(ImGui::Begin("sideBar", NULL, windowFlags)){
        ImVec4 windowBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        ImVec2 padding = ImGui::GetStyle().WindowPadding;
        ImVec4 tabSelectedColor = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        ImVec4 tabButtonColor = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        ImGui::Separator();
        for(int i{0uz}; i < tabs.list.size(); ++i){
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
            if(tabs.index == i){ ImGui::PushStyleColor(ImGuiCol_Button, tabButtonColor); } 
            else { ImGui::PushStyleColor(ImGuiCol_Button, tabSelectedColor); }
            if(ImGui::Button(tabs.list[i].name.c_str(), ImVec2{width-padding.x*2.0f, 0})) tabs.index = i;
            ImGui::PopStyleColor(1);
            ImGui::PopStyleVar();
        }
        ImGui::Separator();
        if(ImGui::Button("Colors")) showingColors = !showingColors;
        ImGui::SameLine();
        if(ImGui::Button("Settings")) showingSettings = !showingSettings;
        ImGui::SameLine();
        if(ImGui::Button("Update")) showingUpdate = !showingUpdate;
        if(showingColors) gui.style.colorUI();
        if(showingSettings) gui.settingsUI();
        if(showingUpdate) gui.updateUI();

        float resizeButtonWidth = 6.0f;
        ImGui::SetCursorPos({width - resizeButtonWidth, 0.0});
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(90, 90, 90, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(130, 130, 130, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(170, 170, 170, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
        ImGui::Button("##resizeBarHandle", {resizeButtonWidth, dim.y});
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if(ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if(ImGui::IsItemActive()) width = storedWidth = std::clamp(width + ImGui::GetIO().MouseDelta.x, winSize.x * 0.05f, winSize.x * 0.5f);
    } ImGui::End();
    ImGui::PopStyleVar(1);
};
