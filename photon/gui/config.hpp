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
    .color4{0.25f, 0.25f, 0.25f, 1.00f},
    .color5{0.15f, 0.15f, 0.15f, 1.00f},
    .color6{0.10f, 0.10f, 0.10f, 1.00f},
    .color7{0.05f, 0.05f, 0.05f, 1.00f},
};

inline constexpr ColorScheme lightMode = {
    .color0{0.13f, 0.12f, 0.11f, 1.00f},
    .color1{0.32f, 0.43f, 0.40f, 1.00f},
    .color2{0.74f, 0.73f, 0.71f, 1.00f},
    .color3{0.86f, 0.85f, 0.83f, 1.00f},
    .color4{0.91f, 0.90f, 0.88f, 1.00f},
    .color5{0.95f, 0.94f, 0.92f, 1.00f},
    .color6{0.97f, 0.96f, 0.94f, 1.00f},
    .color7{0.99f, 0.98f, 0.96f, 1.00f},
};

struct GuiSettings {
  enum SelectedColorMode { dark, light, custom };

  float fontSize = 14.0f;
  ImVec4 colorSeed = ImVec4{0.0f, 0.5f, 0.5f, 1.0f};
  ColorScheme colorScheme = baseColors;
  SelectedColorMode selectedColor = dark;
  ImPlotSpec plotLineSpec{ImPlotProp_LineWeight, 4.0f};
  ImPlotColormap plotColormap = ImPlotColormap_Deep;

  static GuiSettings* get(ImGuiContext* ctx, ImGuiSettingsHandler*);
  static void* readOpenFn(ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name);
  static void readLineFn(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line);
  static void applyAllFn(ImGuiContext* ctx, ImGuiSettingsHandler* handler);
  static void writeAllFn(ImGuiContext* ctx, ImGuiSettingsHandler* handler,
                         ImGuiTextBuffer* out_buf);
  static void regster(GuiSettings* settings);

  void setStyle();
  void colorUI();
  ColorScheme genColors(ImVec4 seed);
};

struct GuiFlags {
  bool showGPUInfo = false;
};
