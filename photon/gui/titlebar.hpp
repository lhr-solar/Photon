#pragma once
#include <SDL3/SDL_dialog.h>
#include <array>
#include "imgui.h"

enum class WindowAction{
    None,
    Close,
    Minimize,
    ToggleMaximize,
};

struct TitleBar{
    int height = 32;
    bool enabled = true;
    int interactiveRectCount = 0;
    WindowAction pendingAction = WindowAction::None;
    static constexpr int buttonCount = 3;
    std::array<SDL_Rect, buttonCount> interactiveRects{};
    void clearInteract();
    void addInteract(const ImVec2& min, const ImVec2& max);
    bool isInteract(int x, int y) const;
};
