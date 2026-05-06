#include <algorithm>

#include "gui.hpp"
#include "sideBar.hpp"
#include "imgui_internal.h"
#include "titlebar.hpp"
#include "imgui.h"
#include "tabs.hpp"
#include "style.hpp"
#include "background_jpg.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

ImTextureData* loadImguiTexture(const unsigned char* data, std::size_t size) {
    int w = 0, h = 0, comp = 0;
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
    if (!pixels) return nullptr;
    ImTextureData* tex = IM_NEW(ImTextureData)();
    tex->Create(ImTextureFormat_RGBA32, w, h);
    memcpy(tex->Pixels, pixels, w * h * 4);
    tex->UseColors = true;

    ImGui::RegisterUserTexture(tex);
    stbi_image_free(pixels);
    return tex;
}

void Sidebar::draw(GUI &gui){
    auto& titleBar = gui.titleBar;
    auto& tabs = gui.tabs;
    ImVec2 winSize = ImGui::GetMainViewport()->Size;
    storedWidth = std::clamp(storedWidth, winSize.x * 0.05f, winSize.x * 0.5f);
    if(!titleBar.showSidebar) { width = 0.0f; return; }
    width = storedWidth;
    float sideBarHeight = winSize.y - (float)titleBar.height;
    ImVec2 pos = {0, (float)titleBar.height};
    ImVec2 dim = {width, sideBarHeight};
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNavFocus;
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(dim);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.1, 0.1});
    if(ImGui::Begin("##pictureWindow", nullptr, windowFlags)){
        if(!backgroundTexture) backgroundTexture = loadImguiTexture(background_jpg, background_jpg_size);
        if(backgroundTexture && backgroundTexture->Status == ImTextureStatus_OK){
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImGui::Image(backgroundTexture->GetTexRef(), winSize);
        }
    }; ImGui::End();
    ImGui::PopStyleVar(1);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(dim);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.0f, 0.0f));
    ImVec4 windowBgColor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {windowBgColor.x, windowBgColor.y, windowBgColor.z, 0.95});
    if(ImGui::Begin("sideBar", NULL, windowFlags)){
        ImDrawList* draw = ImGui::GetWindowDrawList();
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
        float buttonH = ImGui::GetFrameHeightWithSpacing();
        ImVec2 framePadding = ImGui::GetStyle().FramePadding;
        float spacingY = ImGui::GetStyle().ItemSpacing.y;
        float rowH = buttonH + spacingY + framePadding.y;
        pos = {framePadding.x, sideBarHeight - rowH};
        ImGui::SetCursorPos(pos);
        ImGui::Separator();
        if(ImGui::Button("Theme")) ImGui::OpenPopup("Theme");
        ImGui::SameLine();
        if(ImGui::Button("Settings")) ImGui::OpenPopup("Settings");
        ImGui::SameLine();
        if(ImGui::Button("Update")) ImGui::OpenPopup("Update");
        ImGui::SameLine();
        if(ImGui::Button("Export")) ImGui::OpenPopup("Export");
        gui.style.colorUI();
        gui.settingsUI();
        gui.updateUI();
        gui.exportUI();

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
    ImGui::PopStyleColor(1);
};
