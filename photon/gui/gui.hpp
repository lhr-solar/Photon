#pragma once
#include "titlebar.hpp"
#include "sideBar.hpp"
#include "canvas.hpp"
#include "tabs.hpp"
#include "style.hpp"
#include "../parse/spmc.hpp"
#include "../network/network.hpp"
#include "../gpu/gpu.hpp"

struct GUI{
    void init(GPU& gpu);
    void setTabs();
    void destroy();
    void setFont();
    void buildUI();
    void render();
    void settingsUI();
    void updateUI();

    GPU* gpu;

    TitleBar titleBar{};
    Sidebar sideBar{};
    Tabs tabs{};
    Canvas canvas{};
    Style style{};
    SPMCQueue<NetworkCommand, 64> networkCommandBuffer{};
    struct {
        bool showGPUInfo = false;
        bool showColorConfig = false;
    } flags;
};

void ImAnimDemoWindow();
void ImAnimDocWindow();

/* Toggles flag if key is released. If flag is true, executes function */
template <typename F, typename... Args>
decltype(auto) ifKey(ImGuiKey key, bool& flag, F&& func, Args&&... args) {
    if(ImGui::IsKeyReleased(key)) flag = !flag;
    if(flag) std::forward<F>(func)(std::forward<Args>(args)...);
}

template <typename... Args>
void TextRightAligned(const char* fmt, Args... args){
    char buf[128] = {};
    snprintf(buf, sizeof(buf), fmt, args...);
    ImVec2 dims = ImGui::CalcTextSize(buf);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - dims.x);
    ImGui::TextUnformatted(buf);
}
