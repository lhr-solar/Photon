#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"

struct ColorScheme {
    ImVec4 color0{};
    ImVec4 color1{};
    ImVec4 color2{};
    ImVec4 color3{};
    ImVec4 color4{};
    ImVec4 color5{};
    ImVec4 color6{};
    ImVec4 color7{};
};

inline constexpr ColorScheme baseColors = {
    .color0{0.96f, 0.95f, 0.91f, 1.00f},
    .color1{0.90f, 0.88f, 0.82f, 1.00f},
    .color2{0.83f, 0.84f, 0.86f, 1.00f},
    .color3{0.57f, 0.59f, 0.62f, 1.00f},
    .color4{0.18f, 0.18f, 0.19f, 1.00f},
    .color5{0.13f, 0.12f, 0.11f, 1.00f},
    .color6{0.12f, 0.11f, 0.09f, 0.98f},
    .color7{0.09f, 0.08f, 0.07f, 1.00f},
};

inline constexpr ColorScheme lightMode = {
    .color0{0.09f, 0.08f, 0.07f, 1.00f},
    .color1{0.12f, 0.11f, 0.09f, 0.98f},
    .color2{0.13f, 0.12f, 0.11f, 1.00f},
    .color3{0.18f, 0.18f, 0.19f, 1.00f},
    .color4{0.57f, 0.59f, 0.62f, 1.00f},
    .color5{0.83f, 0.84f, 0.86f, 1.00f},
    .color6{0.90f, 0.88f, 0.82f, 1.00f},
    .color7{0.96f, 0.95f, 0.91f, 1.00f},
};

struct GuiSettings{
    enum SelectedColorMode { dark, light, custom };

    float fontSize = 28.0f;
    ImVec4 colorSeed = ImVec4{0.0f, 0.5f, 0.5f, 1.0f};
    ColorScheme colorScheme = baseColors;
    SelectedColorMode selectedColor = dark;
    ImPlotSpec plotLineSpec{};
    ImPlotColormap plotColormap = ImPlotColormap_Deep;

    static GuiSettings* get(ImGuiContext* ctx, ImGuiSettingsHandler*);
    static void* readOpenFn(ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name);
    static void readLineFn(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line);
    static void applyAllFn(ImGuiContext* ctx, ImGuiSettingsHandler* handler);
    static void writeAllFn(ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out_buf);
    static void regster(GuiSettings* settings);

    void setStyle();
    void colorUI();
    ColorScheme genColors(ImVec4 seed);
};

struct GuiFlags{
    bool showGPUInfo = false;
};
