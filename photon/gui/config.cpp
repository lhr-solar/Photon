#include "config.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string_view>

#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imnodes.h"
#include "implot.h"
#include "implot3d.h"

namespace {
ImVec4 withAlpha(ImVec4 color, float alpha) {
  color.w = alpha;
  return color;
}

ImVec4 mixColor(ImVec4 a, ImVec4 b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t);
}

ImU32 colorU32(ImVec4 color) { return ImGui::ColorConvertFloat4ToU32(color); }

struct ThemePalette {
  ImVec4 text;
  ImVec4 muted;
  ImVec4 bg;
  ImVec4 panel;
  ImVec4 raised;
  ImVec4 active;
  ImVec4 accent;
  ImVec4 border;
};

ThemePalette themePalette() {
  const ImGuiStyle& style = ImGui::GetStyle();
  const ImVec4 text = style.Colors[ImGuiCol_Text];
  const ImVec4 bg = style.Colors[ImGuiCol_WindowBg];
  const ImVec4 button = style.Colors[ImGuiCol_Button];
  const ImVec4 accent = style.Colors[ImGuiCol_NavHighlight];
  return ThemePalette{
      .text = text,
      .muted = mixColor(text, bg, 0.48f),
      .bg = bg,
      .panel = mixColor(bg, button, 0.30f),
      .raised = mixColor(bg, button, 0.62f),
      .active = mixColor(button, accent, 0.38f),
      .accent = accent,
      .border = mixColor(button, text, 0.20f),
  };
}

void drawThemeLabel(std::string_view label, const ThemePalette& palette) {
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddText(pos, colorU32(palette.muted), label.data(),
                                      label.data() + label.size());
  ImGui::Dummy(ImGui::CalcTextSize(label.data(), label.data() + label.size()));
}

bool drawThemeButton(const char* id, std::string_view label, ImVec2 size,
                     const ThemePalette& palette, bool selected = false) {
  ImGui::PushID(id);
  ImGui::InvisibleButton("button", size);
  const bool clicked = ImGui::IsItemClicked();
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  const ImGuiID itemId = ImGui::GetItemID();
  const float focus =
      iam_tween_float(itemId, ImHashStr("focus"),
                      selected  ? 1.0f
                      : active  ? 0.88f
                      : hovered ? 0.58f
                                : 0.0f,
                      0.14f, iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade,
                      ImGui::GetIO().DeltaTime, selected ? 1.0f : 0.0f);
  const float press = iam_tween_float(itemId, ImHashStr("press"), active ? 1.0f : 0.0f, 0.08f,
                                      iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade,
                                      ImGui::GetIO().DeltaTime);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled({min.x + press, min.y + press}, {max.x - press, max.y + press},
                      colorU32(withAlpha(mixColor(palette.raised, palette.active, focus), 0.88f)),
                      8.0f);
  draw->AddRect({min.x + press, min.y + press}, {max.x - press, max.y + press},
                colorU32(withAlpha(palette.border, 0.38f + focus * 0.28f)), 8.0f);
  const ImVec2 textSize = ImGui::CalcTextSize(label.data(), label.data() + label.size());
  draw->AddText(
      {min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f + press},
      colorU32(selected ? palette.text : mixColor(palette.muted, palette.text, focus)),
      label.data(), label.data() + label.size());
  ImGui::PopID();
  return clicked;
}

bool drawThemeStepper(const char* id, float& value, float step, float minValue,
                      const ThemePalette& palette) {
  bool changed = false;
  ImGui::PushID(id);
  if (drawThemeButton("minus", "-", {34.0f, 32.0f}, palette)) {
    value = std::max(minValue, value - step);
    changed = true;
  }
  ImGui::SameLine(0.0f, 6.0f);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("value", {54.0f, 32.0f});
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(pos, {pos.x + 54.0f, pos.y + 32.0f},
                      colorU32(withAlpha(palette.panel, 0.82f)), 8.0f);
  char buf[16];
  snprintf(buf, sizeof(buf), "%.0f", value);
  const ImVec2 textSize = ImGui::CalcTextSize(buf);
  draw->AddText({pos.x + (54.0f - textSize.x) * 0.5f, pos.y + (32.0f - textSize.y) * 0.5f},
                colorU32(palette.text), buf);
  ImGui::SameLine(0.0f, 6.0f);
  if (drawThemeButton("plus", "+", {34.0f, 32.0f}, palette)) {
    value += step;
    changed = true;
  }
  ImGui::PopID();
  return changed;
}

}  // namespace

float mixf(float min, float max, float t) { return min + (max - min) * t; }

float randomUnit() {
  static std::mt19937 rng{std::random_device{}()};
  static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(rng);
}

std::array<float, 3> oklabToLinearSrgb(float L, float a, float b) {
  const float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
  const float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
  const float s_ = L - 0.0894841775f * a - 1.2914855480f * b;

  const float l = l_ * l_ * l_;
  const float m = m_ * m_ * m_;
  const float s = s_ * s_ * s_;

  return {
      +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
      -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
      -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s,
  };
}

std::array<float, 3> oklchToOklab(float L, float c, float h) {
  return {L, c * std::cos(h), c * std::sin(h)};
}

ImVec4 tintTowardWhite(ImVec4 color, float amount) {
  return ImVec4(color.x + (1.0f - color.x) * amount, color.y + (1.0f - color.y) * amount,
                color.z + (1.0f - color.z) * amount, 1.0f);
}

float seedToHueBase(ImVec4 seed) {
  const float r = std::clamp(seed.x, 0.0f, 1.0f);
  const float g = std::clamp(seed.y, 0.0f, 1.0f);
  const float b = std::clamp(seed.z, 0.0f, 1.0f);
  const float maxv = std::max({r, g, b});
  const float minv = std::min({r, g, b});
  const float delta = maxv - minv;
  if (delta <= 0.0f) return 0.0f;

  float hue = 0.0f;
  if (maxv == r)
    hue = std::fmod((g - b) / delta, 6.0f);
  else if (maxv == g)
    hue = (b - r) / delta + 2.0f;
  else
    hue = (r - g) / delta + 4.0f;

  hue /= 6.0f;
  if (hue < 0.0f) hue += 1.0f;
  return hue;
}

std::array<float, 3> seedToHsv(ImVec4 seed) {
  const float r = std::clamp(seed.x, 0.0f, 1.0f);
  const float g = std::clamp(seed.y, 0.0f, 1.0f);
  const float b = std::clamp(seed.z, 0.0f, 1.0f);
  const float maxv = std::max({r, g, b});
  const float minv = std::min({r, g, b});
  const float delta = maxv - minv;
  const float saturation = maxv <= 0.0f ? 0.0f : delta / maxv;
  return {seedToHueBase(seed), saturation, maxv};
}

GuiSettings* GuiSettings::get(ImGuiContext* ctx, ImGuiSettingsHandler*) {
  return static_cast<GuiSettings*>(ctx->IO.UserData);
}

void* GuiSettings::readOpenFn(ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name) {
  if (strcmp(name, "UI") != 0 && strcmp(name, "Theme") != 0 && strcmp(name, "Plot") != 0)
    return nullptr;
  return get(ctx, handler);
}

void GuiSettings::readLineFn(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
  GuiSettings* settings = static_cast<GuiSettings*>(entry);
  float f0 = 0.0f;
  float f1 = 0.0f;
  float f2 = 0.0f;
  float f3 = 0.0f;
  int i0 = 0;
  unsigned int u0 = 0;

  if (sscanf(line, "FontSize=%f", &f0) == 1) {
    settings->fontSize = f0;
    return;
  }
  if (sscanf(line, "SelectedColor=%d", &i0) == 1) {
    settings->selectedColor = static_cast<GuiSettings::SelectedColorMode>(i0);
    return;
  }
  if (sscanf(line, "PlotColormap=%d", &i0) == 1) {
    settings->plotColormap = static_cast<ImPlotColormap>(i0);
    return;
  }
  if (sscanf(line, "PlotFlags=%u", &u0) == 1) {
    settings->plotLineSpec.Flags = static_cast<ImPlotItemFlags>(u0);
    return;
  }
  if (sscanf(line, "PlotLineWeight=%f", &f0) == 1) {
    settings->plotLineSpec.LineWeight = f0;
    return;
  }
  if (sscanf(line, "PlotFillAlpha=%f", &f0) == 1) {
    settings->plotLineSpec.FillAlpha = f0;
    return;
  }
  if (sscanf(line, "PlotMarker=%d", &i0) == 1) {
    settings->plotLineSpec.Marker = static_cast<ImPlotMarker>(i0);
    return;
  }
  if (sscanf(line, "PlotMarkerSize=%f", &f0) == 1) {
    settings->plotLineSpec.MarkerSize = f0;
    return;
  }
  if (sscanf(line, "PlotSize=%f", &f0) == 1) {
    settings->plotLineSpec.Size = f0;
    return;
  }
  if (sscanf(line, "PlotOffset=%d", &i0) == 1) {
    settings->plotLineSpec.Offset = i0;
    return;
  }
  if (sscanf(line, "PlotStride=%d", &i0) == 1) {
    settings->plotLineSpec.Stride = i0;
    return;
  }
  if (sscanf(line, "ColorSeed=%f,%f,%f,%f", &f0, &f1, &f2, &f3) == 4) {
    settings->colorSeed = ImVec4(f0, f1, f2, f3);
    return;
  }
  if (sscanf(line, "LineColor=%f,%f,%f,%f", &f0, &f1, &f2, &f3) == 4) {
    settings->plotLineSpec.LineColor = ImVec4(f0, f1, f2, f3);
    return;
  }
  if (sscanf(line, "FillColor=%f,%f,%f,%f", &f0, &f1, &f2, &f3) == 4) {
    settings->plotLineSpec.FillColor = ImVec4(f0, f1, f2, f3);
    return;
  }
  if (sscanf(line, "MarkerLineColor=%f,%f,%f,%f", &f0, &f1, &f2, &f3) == 4) {
    settings->plotLineSpec.MarkerLineColor = ImVec4(f0, f1, f2, f3);
    return;
  }
  if (sscanf(line, "MarkerFillColor=%f,%f,%f,%f", &f0, &f1, &f2, &f3) == 4) {
    settings->plotLineSpec.MarkerFillColor = ImVec4(f0, f1, f2, f3);
    return;
  }
  if (sscanf(line, "Color0=%f,%f,%f,%f", &f0, &f1, &f2, &f3) == 4) {
    settings->colorScheme.color0 = ImVec4(f0, f1, f2, f3);
    return;
  }
  if (sscanf(line, "Color1=%f,%f,%f,%f", &f0, &f1, &f2, &f3) == 4) {
    settings->colorScheme.color1 = ImVec4(f0, f1, f2, f3);
    return;
  }
  if (sscanf(line, "Color2=%f,%f,%f,%f", &f0, &f1, &f2, &f3) == 4) {
    settings->colorScheme.color2 = ImVec4(f0, f1, f2, f3);
    return;
  }
  if (sscanf(line, "Color3=%f,%f,%f,%f", &f0, &f1, &f2, &f3) == 4) {
    settings->colorScheme.color3 = ImVec4(f0, f1, f2, f3);
    return;
  }
  if (sscanf(line, "Color4=%f,%f,%f,%f", &f0, &f1, &f2, &f3) == 4) {
    settings->colorScheme.color4 = ImVec4(f0, f1, f2, f3);
    return;
  }
  if (sscanf(line, "Color5=%f,%f,%f,%f", &f0, &f1, &f2, &f3) == 4) {
    settings->colorScheme.color5 = ImVec4(f0, f1, f2, f3);
    return;
  }
  if (sscanf(line, "Color6=%f,%f,%f,%f", &f0, &f1, &f2, &f3) == 4) {
    settings->colorScheme.color6 = ImVec4(f0, f1, f2, f3);
    return;
  }
  if (sscanf(line, "Color7=%f,%f,%f,%f", &f0, &f1, &f2, &f3) == 4) {
    settings->colorScheme.color7 = ImVec4(f0, f1, f2, f3);
    return;
  }
}

void GuiSettings::applyAllFn(ImGuiContext* ctx, ImGuiSettingsHandler* handler) {
  GuiSettings* settings = get(ctx, handler);
  if (settings->selectedColor == dark) settings->colorScheme = baseColors;
  if (settings->selectedColor == light) settings->colorScheme = lightMode;
  settings->setStyle();
  ctx->Style._NextFrameFontSizeBase = settings->fontSize;
}

void GuiSettings::writeAllFn(ImGuiContext* ctx, ImGuiSettingsHandler* handler,
                             ImGuiTextBuffer* out_buf) {
  GuiSettings* settings = get(ctx, handler);
  out_buf->appendf("[Photon][UI]\n");
  out_buf->appendf("FontSize=%.1f\n", settings->fontSize);
  out_buf->append("\n");

  out_buf->appendf("[Photon][Theme]\n");
  out_buf->appendf("SelectedColor=%d\n", static_cast<int>(settings->selectedColor));
  out_buf->appendf("ColorSeed=%.6f,%.6f,%.6f,%.6f\n", settings->colorSeed.x, settings->colorSeed.y,
                   settings->colorSeed.z, settings->colorSeed.w);
  out_buf->appendf("Color0=%.6f,%.6f,%.6f,%.6f\n", settings->colorScheme.color0.x,
                   settings->colorScheme.color0.y, settings->colorScheme.color0.z,
                   settings->colorScheme.color0.w);
  out_buf->appendf("Color1=%.6f,%.6f,%.6f,%.6f\n", settings->colorScheme.color1.x,
                   settings->colorScheme.color1.y, settings->colorScheme.color1.z,
                   settings->colorScheme.color1.w);
  out_buf->appendf("Color2=%.6f,%.6f,%.6f,%.6f\n", settings->colorScheme.color2.x,
                   settings->colorScheme.color2.y, settings->colorScheme.color2.z,
                   settings->colorScheme.color2.w);
  out_buf->appendf("Color3=%.6f,%.6f,%.6f,%.6f\n", settings->colorScheme.color3.x,
                   settings->colorScheme.color3.y, settings->colorScheme.color3.z,
                   settings->colorScheme.color3.w);
  out_buf->appendf("Color4=%.6f,%.6f,%.6f,%.6f\n", settings->colorScheme.color4.x,
                   settings->colorScheme.color4.y, settings->colorScheme.color4.z,
                   settings->colorScheme.color4.w);
  out_buf->appendf("Color5=%.6f,%.6f,%.6f,%.6f\n", settings->colorScheme.color5.x,
                   settings->colorScheme.color5.y, settings->colorScheme.color5.z,
                   settings->colorScheme.color5.w);
  out_buf->appendf("Color6=%.6f,%.6f,%.6f,%.6f\n", settings->colorScheme.color6.x,
                   settings->colorScheme.color6.y, settings->colorScheme.color6.z,
                   settings->colorScheme.color6.w);
  out_buf->appendf("Color7=%.6f,%.6f,%.6f,%.6f\n", settings->colorScheme.color7.x,
                   settings->colorScheme.color7.y, settings->colorScheme.color7.z,
                   settings->colorScheme.color7.w);
  out_buf->append("\n");

  out_buf->appendf("[Photon][Plot]\n");
  out_buf->appendf("PlotColormap=%d\n", static_cast<int>(settings->plotColormap));
  out_buf->appendf("LineColor=%.6f,%.6f,%.6f,%.6f\n", settings->plotLineSpec.LineColor.x,
                   settings->plotLineSpec.LineColor.y, settings->plotLineSpec.LineColor.z,
                   settings->plotLineSpec.LineColor.w);
  out_buf->appendf("PlotLineWeight=%.6f\n", settings->plotLineSpec.LineWeight);
  out_buf->appendf("FillColor=%.6f,%.6f,%.6f,%.6f\n", settings->plotLineSpec.FillColor.x,
                   settings->plotLineSpec.FillColor.y, settings->plotLineSpec.FillColor.z,
                   settings->plotLineSpec.FillColor.w);
  out_buf->appendf("PlotFillAlpha=%.6f\n", settings->plotLineSpec.FillAlpha);
  out_buf->appendf("PlotMarker=%d\n", static_cast<int>(settings->plotLineSpec.Marker));
  out_buf->appendf("PlotMarkerSize=%.6f\n", settings->plotLineSpec.MarkerSize);
  out_buf->appendf(
      "MarkerLineColor=%.6f,%.6f,%.6f,%.6f\n", settings->plotLineSpec.MarkerLineColor.x,
      settings->plotLineSpec.MarkerLineColor.y, settings->plotLineSpec.MarkerLineColor.z,
      settings->plotLineSpec.MarkerLineColor.w);
  out_buf->appendf(
      "MarkerFillColor=%.6f,%.6f,%.6f,%.6f\n", settings->plotLineSpec.MarkerFillColor.x,
      settings->plotLineSpec.MarkerFillColor.y, settings->plotLineSpec.MarkerFillColor.z,
      settings->plotLineSpec.MarkerFillColor.w);
  out_buf->appendf("PlotSize=%.6f\n", settings->plotLineSpec.Size);
  out_buf->appendf("PlotOffset=%d\n", settings->plotLineSpec.Offset);
  out_buf->appendf("PlotStride=%d\n", settings->plotLineSpec.Stride);
  out_buf->appendf("PlotFlags=%u\n", static_cast<unsigned int>(settings->plotLineSpec.Flags));
  out_buf->append("\n");
}

void GuiSettings::regster(GuiSettings* settings) {
  ImGuiIO& io = ImGui::GetIO();
  io.UserData = settings;
  if (ImGui::FindSettingsHandler("Photon")) return;

  ImGuiSettingsHandler handler;
  handler.TypeName = "Photon";
  handler.TypeHash = ImHashStr("Photon");
  handler.ReadOpenFn = readOpenFn;
  handler.ReadLineFn = readLineFn;
  handler.ApplyAllFn = applyAllFn;
  handler.WriteAllFn = writeAllFn;
  ImGui::AddSettingsHandler(&handler);
}

void GuiSettings::setStyle() {
  ImGuiStyle& style = ImGui::GetStyle();
  style.FontSizeBase = fontSize;
  style.Alpha = 1.0f;
  style.DisabledAlpha = 0.55f;
  style.TabRounding = 0.0f;
  style.WindowRounding = 0.0f;
  style.ChildRounding = 0.0f;
  style.PopupRounding = 0.0f;
  style.GrabRounding = 8.0f;
  style.ScrollbarRounding = 0.0f;
  style.FrameRounding = 0.0f;
  style.FrameBorderSize = 0.0f;
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.DockingSeparatorSize = 1.0f;
  style.WindowPadding = ImVec2{8.0f, 5.0f};
  style.FramePadding = ImVec2{12.0f, 9.0f};
  style.CellPadding = ImVec2{14.0f, 8.0f};
  style.ItemSpacing = ImVec2{10.0f, 8.0f};
  style.ItemInnerSpacing = ImVec2{8.0f, 6.0f};
  style.IndentSpacing = 18.0f;
  style.ScrollbarSize = 12.0f;
  style.GrabMinSize = 10.0f;

  auto color0 = colorScheme.color0;
  auto color1 = colorScheme.color1;
  auto color2 = colorScheme.color2;
  auto color3 = colorScheme.color3;
  auto color4 = colorScheme.color4;
  auto color5 = colorScheme.color5;
  auto color6 = colorScheme.color6;
  auto color7 = colorScheme.color7;

  ImVec4 red = {1.0, 0.0, 0.0, 1.0};
  ImVec4* colors = style.Colors;
  colors[ImGuiCol_Text] = color0;
  colors[ImGuiCol_TextDisabled] = color3;
  colors[ImGuiCol_WindowBg] = color7;
  colors[ImGuiCol_ChildBg] = color7;
  colors[ImGuiCol_PopupBg] = color7;
  colors[ImGuiCol_Border] = color4;
  colors[ImGuiCol_BorderShadow] = color7;
  colors[ImGuiCol_FrameBg] = color4;
  colors[ImGuiCol_FrameBgHovered] = color3;
  colors[ImGuiCol_FrameBgActive] = color4;
  colors[ImGuiCol_TitleBg] = color6;
  colors[ImGuiCol_TitleBgActive] = color5;
  colors[ImGuiCol_TitleBgCollapsed] = color7;
  colors[ImGuiCol_MenuBarBg] = color5;
  colors[ImGuiCol_ScrollbarBg] = color2;
  colors[ImGuiCol_ScrollbarGrab] = color3;
  colors[ImGuiCol_ScrollbarGrabHovered] = color1;
  colors[ImGuiCol_ScrollbarGrabActive] = color0;
  colors[ImGuiCol_CheckMark] = color1;
  colors[ImGuiCol_SliderGrab] = color2;
  colors[ImGuiCol_SliderGrabActive] = color1;
  colors[ImGuiCol_Button] = color4;
  colors[ImGuiCol_ButtonHovered] = color3;
  colors[ImGuiCol_ButtonActive] = color7;
  colors[ImGuiCol_Header] = color4;
  colors[ImGuiCol_HeaderHovered] = color3;
  colors[ImGuiCol_HeaderActive] = color2;
  colors[ImGuiCol_Separator] = color3;
  colors[ImGuiCol_SeparatorHovered] = color2;
  colors[ImGuiCol_SeparatorActive] = color1;
  colors[ImGuiCol_ResizeGrip] = color2;
  colors[ImGuiCol_ResizeGripHovered] = color1;
  colors[ImGuiCol_ResizeGripActive] = color0;
  colors[ImGuiCol_Tab] = color7;
  colors[ImGuiCol_TabHovered] = color3;
  colors[ImGuiCol_TabActive] = color4;
  colors[ImGuiCol_TabUnfocused] = color7;
  colors[ImGuiCol_TabUnfocusedActive] = color4;
  colors[ImGuiCol_TabSelectedOverline] = color4;
  colors[ImGuiCol_DockingPreview] = color1;
  colors[ImGuiCol_DockingEmptyBg] = color1;
  colors[ImGuiCol_PlotLines] = color1;
  colors[ImGuiCol_PlotLinesHovered] = color2;
  colors[ImGuiCol_PlotHistogram] = color1;
  colors[ImGuiCol_PlotHistogramHovered] = color2;
  colors[ImGuiCol_TableHeaderBg] = color3;
  colors[ImGuiCol_TableBorderStrong] = color4;
  colors[ImGuiCol_TableBorderLight] = color4;
  colors[ImGuiCol_TableRowBg] = color5;
  colors[ImGuiCol_TableRowBgAlt] = color6;
  colors[ImGuiCol_TextSelectedBg] = color3;
  colors[ImGuiCol_DragDropTarget] = color3;
  colors[ImGuiCol_NavHighlight] = color1;
  colors[ImGuiCol_NavWindowingHighlight] = color1;
  colors[ImGuiCol_NavWindowingDimBg] = {color1.x, color1.y, color1.z, 0.2f};
  colors[ImGuiCol_ModalWindowDimBg] = {color0.x, color0.y, color0.z, 0.4f};

  ImPlotStyle& plotStyle = ImPlot::GetStyle();
  plotStyle.UseLocalTime = true;
  colors = plotStyle.Colors;
  colors[ImPlotCol_FrameBg] = color5;
  colors[ImPlotCol_PlotBg] = color4;
  colors[ImPlotCol_PlotBorder] = color5;
  colors[ImPlotCol_LegendBg] = color4;
  colors[ImPlotCol_LegendBorder] = color5;
  colors[ImPlotCol_LegendText] = color0;
  colors[ImPlotCol_TitleText] = color0;
  colors[ImPlotCol_InlayText] = color1;
  colors[ImPlotCol_AxisText] = color1;
  colors[ImPlotCol_AxisGrid] = color1;
  colors[ImPlotCol_AxisTick] = color1;
  colors[ImPlotCol_AxisBg] = color5;
  colors[ImPlotCol_AxisBgHovered] = color4;
  colors[ImPlotCol_AxisBgActive] = color3;
  colors[ImPlotCol_Selection] = color1;
  colors[ImPlotCol_Crosshairs] = color1;
  plotStyle.Colormap = plotColormap;

  ImPlot3DStyle& tplotStyle = ImPlot3D::GetStyle();
  colors = tplotStyle.Colors;
  colors[ImPlot3DCol_TitleText] = color0;
  colors[ImPlot3DCol_InlayText] = color1;
  colors[ImPlot3DCol_FrameBg] = color5;
  colors[ImPlot3DCol_PlotBg] = color4;
  colors[ImPlot3DCol_PlotBorder] = color5;
  colors[ImPlot3DCol_LegendBg] = color4;
  colors[ImPlot3DCol_LegendBorder] = color5;
  colors[ImPlot3DCol_LegendText] = color0;
  colors[ImPlot3DCol_AxisText] = color1;
  colors[ImPlot3DCol_AxisGrid] = color1;
  colors[ImPlot3DCol_AxisTick] = color1;

  ImNodesStyle& nodeStyle = ImNodes::GetStyle();
  const auto packNodeColor = [](const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
  };

  nodeStyle.Colors[ImNodesCol_NodeBackground] = packNodeColor(color5);
  nodeStyle.Colors[ImNodesCol_NodeBackgroundHovered] = packNodeColor(color4);
  nodeStyle.Colors[ImNodesCol_NodeBackgroundSelected] = packNodeColor(color3);
  nodeStyle.Colors[ImNodesCol_NodeOutline] = packNodeColor(color5);
  nodeStyle.Colors[ImNodesCol_TitleBar] = packNodeColor(color5);
  nodeStyle.Colors[ImNodesCol_TitleBarHovered] = packNodeColor(color4);
  nodeStyle.Colors[ImNodesCol_TitleBarSelected] = packNodeColor(color3);
  nodeStyle.Colors[ImNodesCol_Link] = packNodeColor(color2);
  nodeStyle.Colors[ImNodesCol_LinkHovered] = packNodeColor(color1);
  nodeStyle.Colors[ImNodesCol_LinkSelected] = packNodeColor(color0);
  nodeStyle.Colors[ImNodesCol_Pin] = packNodeColor(color2);
  nodeStyle.Colors[ImNodesCol_PinHovered] = packNodeColor(color0);
  nodeStyle.Colors[ImNodesCol_BoxSelector] = packNodeColor({color0.x, color0.y, color0.z, 0.2f});
  nodeStyle.Colors[ImNodesCol_BoxSelectorOutline] = packNodeColor(color1);
  nodeStyle.Colors[ImNodesCol_GridBackground] = packNodeColor(color7);
  nodeStyle.Colors[ImNodesCol_GridLine] = packNodeColor(color4);
  nodeStyle.Colors[ImNodesCol_GridLinePrimary] = packNodeColor(color3);
  nodeStyle.Colors[ImNodesCol_MiniMapBackground] = packNodeColor(color4);
  nodeStyle.Colors[ImNodesCol_MiniMapBackgroundHovered] = packNodeColor(color6);
  nodeStyle.Colors[ImNodesCol_MiniMapOutline] = packNodeColor(color4);
  nodeStyle.Colors[ImNodesCol_MiniMapOutlineHovered] = packNodeColor(color1);
  nodeStyle.Colors[ImNodesCol_MiniMapNodeBackground] = packNodeColor(color3);
  nodeStyle.Colors[ImNodesCol_MiniMapNodeBackgroundHovered] = packNodeColor(color2);
  nodeStyle.Colors[ImNodesCol_MiniMapNodeBackgroundSelected] = packNodeColor(color1);
  nodeStyle.Colors[ImNodesCol_MiniMapNodeOutline] = packNodeColor(color4);
  nodeStyle.Colors[ImNodesCol_MiniMapLink] = packNodeColor(color1);
  nodeStyle.Colors[ImNodesCol_MiniMapLinkSelected] = packNodeColor(color0);
  nodeStyle.Colors[ImNodesCol_MiniMapCanvas] = packNodeColor({0.0f, 0.0f, 0.0f, 0.0f});
  nodeStyle.Colors[ImNodesCol_MiniMapCanvasOutline] = packNodeColor(color3);
}

ColorScheme GuiSettings::genColors(ImVec4 seed) {
  struct Settings {
    float hueBase;
    float hueContrast;
    float saturationBase;
    float saturationContrast;
    float luminanceBase;
    float luminanceContrast;
    float fixed;
    bool saturationConstant;
    int colorCount;
  };

  const auto hsv = seedToHsv(seed);

  Settings settings{
      .hueBase = hsv[0],
      .hueContrast = 0.8f,
      .saturationBase = randomUnit(),
      .saturationContrast = randomUnit(),
      .luminanceBase = hsv[2],
      .luminanceContrast = 0.8f,
      .fixed = 0.1f,
      .saturationConstant = true,
      .colorCount = 8,
  };

  constexpr float kPi = 3.14159265358979323846f;
  const float hueBase = settings.hueBase * 2.0f * kPi;
  const float hueContrast = mixf(0.33f, 1.0f, settings.hueContrast);
  const float chromaBase = mixf(0.01f, 0.1f, settings.saturationBase);
  const float chromaContrast = mixf(0.075f, 0.125f - chromaBase, settings.saturationContrast);
  const float chromaFixed = mixf(0.01f, 0.125f, settings.fixed);
  const float lightnessBase = mixf(0.3f, 0.6f, settings.luminanceBase);
  const float lightnessContrast = mixf(0.3f, 1.0f - lightnessBase, settings.luminanceContrast);
  const float lightnessFixed = mixf(0.6f, 0.9f, settings.fixed);

  ColorScheme scheme{};
  ImVec4* colors[] = {&scheme.color7, &scheme.color6, &scheme.color5, &scheme.color4,
                      &scheme.color3, &scheme.color2, &scheme.color1, &scheme.color0};

  for (int i = 0; i < settings.colorCount; ++i) {
    const float linearIterator =
        static_cast<float>(i) / static_cast<float>(settings.colorCount - 1);
    float hueOffset = linearIterator * hueContrast * 2.0f * kPi + (kPi / 4.0f);
    hueOffset *= 0.33f;
    hueOffset += (randomUnit() * 2.0f - 1.0f) * 0.01f;

    float chroma = chromaBase + linearIterator * chromaContrast;
    float lightness = lightnessBase + linearIterator * lightnessContrast;
    if (settings.saturationConstant)
      chroma = chromaFixed;
    else
      lightness = lightnessFixed;

    const auto lab = oklchToOklab(lightness, chroma, hueBase + hueOffset);
    auto rgb = oklabToLinearSrgb(lab[0], lab[1], lab[2]);
    rgb[0] = std::clamp(rgb[0], 0.0f, 1.0f);
    rgb[1] = std::clamp(rgb[1], 0.0f, 1.0f);
    rgb[2] = std::clamp(rgb[2], 0.0f, 1.0f);
    *colors[i] = ImVec4(rgb[0], rgb[1], rgb[2], 1.0f);
  }

  scheme.color0 = tintTowardWhite(scheme.color0, 0.9f);
  return scheme;
}

void GuiSettings::colorUI() {
  bool dirty = false;
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 displaySize = io.DisplaySize;
  const ImVec2 winSize{std::min(780.0f, std::max(360.0f, displaySize.x - 64.0f)),
                       std::min(600.0f, std::max(420.0f, displaySize.y - 64.0f))};
  const ImVec2 winPos{(displaySize.x - winSize.x) * 0.5f, (displaySize.y - winSize.y) * 0.5f};
  ImGui::SetNextWindowSize(winSize, ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(winPos, ImGuiCond_Appearing);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 14.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
  const ThemePalette palette = themePalette();
  ImGui::PushStyleColor(ImGuiCol_PopupBg, withAlpha(palette.bg, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, withAlpha(palette.bg, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(palette.border, 0.70f));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
  if (ImGui::BeginPopupModal("Theme", nullptr, flags)) {
    const float hueMax = 0.999f;
    const float valueMin = 0.001f;
    const float rounding = 8.0f;
    float hue = 0.0f;
    float saturation = 0.0f;
    float value = 0.0f;
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    ImDrawList* draw = ImGui::GetWindowDrawList();

    drawThemeLabel("Theme", palette);

    const float segmentGap = 8.0f;
    const float segmentW = (contentWidth - segmentGap * 2.0f) / 3.0f;
    const float segmentH = 38.0f;
    if (drawThemeButton("ThemeDefault", "Default", {segmentW, segmentH}, palette,
                        selectedColor == dark)) {
      selectedColor = dark;
      colorScheme = baseColors;
      dirty = true;
    }
    ImGui::SameLine(0.0f, segmentGap);
    if (drawThemeButton("ThemeLight", "Light", {segmentW, segmentH}, palette,
                        selectedColor == light)) {
      selectedColor = light;
      colorScheme = lightMode;
      dirty = true;
    }
    ImGui::SameLine(0.0f, segmentGap);
    if (drawThemeButton("ThemeCustom", "Custom", {segmentW, segmentH}, palette,
                        selectedColor == custom)) {
      selectedColor = custom;
      colorScheme = genColors(colorSeed);
      dirty = true;
    }

    ImGui::ColorConvertRGBtoHSV(colorSeed.x, colorSeed.y, colorSeed.z, hue, saturation, value);

    const float footerHeight = 40.0f;
    const float bodyHeight = std::max(260.0f, ImGui::GetContentRegionAvail().y - footerHeight);
    const float columnGap = 10.0f;
    const float leftWidth = std::min(320.0f, contentWidth * 0.44f);
    const float rightWidth = contentWidth - leftWidth - columnGap;

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, withAlpha(palette.panel, 0.76f));
    ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(palette.border, 0.48f));

    constexpr ImGuiWindowFlags childFlags =
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;

    ImGui::BeginChild("##ThemeColorPanel", {leftWidth, bodyHeight}, ImGuiChildFlags_Borders,
                      childFlags);
    drawThemeLabel("Font size", palette);
    if (drawThemeStepper("FontSize", fontSize, 1.0f, 1.0f, palette)) dirty = true;
    ImGui::Spacing();
    const float gap = 8.0f;
    const float barWidth = 16.0f;
    const float maxPickerWidth =
        leftWidth - 2.0f * ImGui::GetStyle().WindowPadding.x - 2.0f * (barWidth + gap);
    const float size = std::clamp(bodyHeight * 0.34f, 96.0f, std::min(160.0f, maxPickerWidth));
    ImGui::BeginGroup();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##HueValueField", {size, size});
    bool seedEdited = false;
    if (ImGui::IsItemActive() ||
        (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
      ImVec2 m = ImGui::GetIO().MousePos;
      hue = std::clamp((m.x - p.x) / size, 0.0f, hueMax);
      value = std::clamp(1.0f - (m.y - p.y) / size, valueMin, 1.0f);
      seedEdited = true;
      dirty = true;
    }
    const ImU32 hues[] = {IM_COL32(255, 0, 0, 255), IM_COL32(255, 255, 0, 255),
                          IM_COL32(0, 255, 0, 255), IM_COL32(0, 255, 255, 255),
                          IM_COL32(0, 0, 255, 255), IM_COL32(255, 0, 255, 255),
                          IM_COL32(255, 0, 0, 255)};
    for (int i = 0; i < 6; ++i) {
      float x0 = p.x + size * (static_cast<float>(i) / 6.0f);
      float x1 = p.x + size * (static_cast<float>(i + 1) / 6.0f);
      draw->AddRectFilledMultiColor({x0, p.y}, {x1, p.y + size}, hues[i], hues[i + 1], hues[i + 1],
                                    hues[i]);
    }
    draw->AddRectFilledMultiColor(p, {p.x + size, p.y + size}, 0, 0, IM_COL32(0, 0, 0, 255),
                                  IM_COL32(0, 0, 0, 255));
    draw->AddRect(p, {p.x + size, p.y + size}, colorU32(withAlpha(palette.border, 0.72f)),
                  rounding);
    draw->AddCircle({p.x + hue * size, p.y + (1.0f - value) * size}, 5.0f,
                    IM_COL32(255, 255, 255, 255), 0, 2.0f);

    ImGui::SameLine(0.0f, gap);
    ImVec2 huePos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##HueBar", {barWidth, size});
    if (ImGui::IsItemActive() ||
        (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
      ImVec2 m = ImGui::GetIO().MousePos;
      hue = std::clamp((m.y - huePos.y) / size, 0.0f, hueMax);
      seedEdited = true;
      dirty = true;
    }
    for (int i = 0; i < 6; ++i) {
      float y0 = huePos.y + size * (static_cast<float>(i) / 6.0f);
      float y1 = huePos.y + size * (static_cast<float>(i + 1) / 6.0f);
      draw->AddRectFilledMultiColor({huePos.x, y0}, {huePos.x + barWidth, y1}, hues[i], hues[i],
                                    hues[i + 1], hues[i + 1]);
    }
    draw->AddRect(huePos, {huePos.x + barWidth, huePos.y + size},
                  colorU32(withAlpha(palette.border, 0.72f)), 8.0f);
    draw->AddLine({huePos.x, huePos.y + hue * size}, {huePos.x + barWidth, huePos.y + hue * size},
                  IM_COL32(255, 255, 255, 255), 2.0f);

    ImGui::SameLine(0.0f, gap);
    ImVec2 valuePos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##ValueBar", {barWidth, size});
    if (ImGui::IsItemActive() ||
        (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
      ImVec2 m = ImGui::GetIO().MousePos;
      value = std::clamp(1.0f - (m.y - valuePos.y) / size, valueMin, 1.0f);
      seedEdited = true;
      dirty = true;
    }
    float vr = 0.0f;
    float vg = 0.0f;
    float vb = 0.0f;
    ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, vr, vg, vb);
    draw->AddRectFilledMultiColor(
        valuePos, {valuePos.x + barWidth, valuePos.y + size},
        IM_COL32(static_cast<int>(vr * 255.0f), static_cast<int>(vg * 255.0f),
                 static_cast<int>(vb * 255.0f), 255),
        IM_COL32(static_cast<int>(vr * 255.0f), static_cast<int>(vg * 255.0f),
                 static_cast<int>(vb * 255.0f), 255),
        IM_COL32(0, 0, 0, 255), IM_COL32(0, 0, 0, 255));
    draw->AddRect(valuePos, {valuePos.x + barWidth, valuePos.y + size},
                  colorU32(withAlpha(palette.border, 0.72f)), 8.0f);
    draw->AddLine({valuePos.x, valuePos.y + (1.0f - value) * size},
                  {valuePos.x + barWidth, valuePos.y + (1.0f - value) * size},
                  IM_COL32(255, 255, 255, 255), 2.0f);

    ImGui::ColorConvertHSVtoRGB(hue, 1.0f, value, colorSeed.x, colorSeed.y, colorSeed.z);
    ImGui::EndGroup();
    if (seedEdited) {
      selectedColor = custom;
      colorScheme = genColors(colorSeed);
    }
    const ImVec4 cv[] = {colorScheme.color0, colorScheme.color1, colorScheme.color2,
                         colorScheme.color3, colorScheme.color4, colorScheme.color5,
                         colorScheme.color6, colorScheme.color7};

    ImGui::Spacing();
    ImVec2 palettePos = ImGui::GetCursorScreenPos();
    const float paletteWidth = leftWidth - 2.0f * ImGui::GetStyle().WindowPadding.x;
    const float paletteFit = (paletteWidth - 3.0f * gap) / 4.0f;
    const float paletteHeightFit = std::max(24.0f, (bodyHeight - size - 142.0f) * 0.5f);
    const float paletteSize = std::clamp(std::min(paletteFit, paletteHeightFit), 24.0f, 58.0f);
    for (int i = 0; i < 8; ++i) {
      ImVec2 min = {palettePos.x + (i % 4) * (paletteSize + gap),
                    palettePos.y + (i / 4) * (paletteSize + gap)};
      ImVec2 max = {min.x + paletteSize, min.y + paletteSize};
      draw->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(cv[i]), rounding);
      draw->AddRect(min, max, colorU32(withAlpha(palette.border, 0.55f)), rounding);
    }
    ImGui::Dummy({paletteWidth, 2.0f * paletteSize + gap});
    ImGui::EndChild();

    ImGui::SameLine(0.0f, columnGap);
    ImGui::BeginChild("##ThemePlotPanel", {rightWidth, bodyHeight}, ImGuiChildFlags_Borders,
                      childFlags);
    drawThemeLabel("Line Thickness", palette);
    if (plotLineSpec.LineWeight < 1.0f) plotLineSpec.LineWeight = 1.0f;
    if (drawThemeStepper("LineWeight", plotLineSpec.LineWeight, 1.0f, 1.0f, palette)) dirty = true;
    ImGui::Spacing();
    drawThemeLabel("Colormap", palette);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, withAlpha(palette.raised, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, withAlpha(palette.active, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_Button, withAlpha(palette.raised, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, withAlpha(palette.active, 0.72f));
    if (ImPlot::ShowCustomColormapSelector("##ImPlotColormap")) {
      plotColormap = ImPlot::GetStyle().Colormap;
      dirty = true;
    }
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);

    ImGui::Spacing();
    drawThemeLabel("Preview", palette);
    std::array<double, 2> time = {0.0, 5.0};
    std::array<double, 2> points = {0.0, 2.0};
    const float plotHeight = std::max(140.0f, ImGui::GetContentRegionAvail().y);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, withAlpha(palette.bg, 0.40f));
    if (ImPlot::BeginPlot("##colorPalettePlot", {-FLT_MIN, plotHeight}, ImPlotFlags_NoInputs)) {
      for (int i{0}; i < 10; i++) {
        char buf[10];
        snprintf(buf, sizeof(buf), "##Color %i", i);
        ImPlot::PlotLine((const char*)buf, time.data(), points.data(),
                         static_cast<int>(points.size()), plotLineSpec);
        points[1] += 2;
      }
      ImPlot::EndPlot();
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 96.0f);
    if (dirty) ImGui::MarkIniSettingsDirty();
    if (drawThemeButton("CloseTheme", "Close", {96.0f, 34.0f}, palette)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(4);
}
