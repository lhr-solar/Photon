#pragma once
#include "titlebar.hpp"
#include "sideBar.hpp"
#include "canvas.hpp"
#include "tabs.hpp"
#include "style.hpp"
#include "config.hpp"
#include "../parse/spmc.hpp"
#include "../network/network.hpp"
#include "../gpu/gpu.hpp"
#include "../gpu/shader.hpp"

struct GUI{
    void init(GPU& gpu);
    void setTabs();
    void destroy();
    void setFont();
    void buildUI();
    void render();
    void settingsUI();
    void updateUI();
    void exportUI();

    void shaderTest();

    GPU* gpu;

    TitleBar titleBar{};
    Sidebar sideBar{};
    Tabs tabs{};
    Canvas canvas{};
    Style style{};
    SPMCQueue<NetworkCommand, 64> networkCommandBuffer{};
    Shader testShader{};
    GuiSettings settings{};
    GuiFlags flags{};
};

/* forward function handles */
void ImAnimDemoWindow();
void ImAnimDocWindow();

/* Toggles flag if key is released. If flag is true, executes function */
template <typename F, typename... Args>
decltype(auto) ifKey(ImGuiKey key, bool& flag, F&& func, Args&&... args) {
    if(ImGui::IsKeyReleased(key)) flag = !flag;
    if(flag) std::forward<F>(func)(std::forward<Args>(args)...);
}

/* aligns the given text to rhs of the window */
template <typename... Args>
void TextRightAligned(const char* fmt, Args... args){
    char buf[128] = {};
    snprintf(buf, sizeof(buf), fmt, args...);
    ImVec2 dims = ImGui::CalcTextSize(buf);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - dims.x);
    ImGui::TextUnformatted(buf);
}
