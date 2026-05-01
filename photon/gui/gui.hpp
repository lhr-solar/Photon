#pragma once
#include "titlebar.hpp"
#include "style.hpp"
#include "../parse/spmc.hpp"
#include "../network/network.hpp"
#include "../gpu/gpu.hpp"

struct GUI{
    void init(GPU& gpu);
    void destroy();
    void setFont();
    void buildUI();
    void render();
    void setStyle();
    void showColors(colorScheme& colors);
    void drawTest();
    void animTextBox(std::string_view text, bool start);
    void animLine(ImVec2 begin, ImVec2 end, bool start);

    GPU* gpu;
    TitleBar titleBar{};
    colorScheme colors = baseColors;
    SPMCQueue<NetworkCommand, 64> networkCommandBuffer{};
    struct {
        bool showGPUInfo = false;
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
