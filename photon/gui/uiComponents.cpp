#include "uiComponents.hpp"

#include <algorithm>
#include <cfloat>

#include "im_anim.h"
#include "imgui_internal.h"

namespace PhotonUi {
namespace {
constexpr int kModalStyleVarCount = 6;
constexpr int kTooltipStyleVarCount = 4;
constexpr int kTooltipStyleColorCount = 3;
constexpr int kContentStyleVarCount = 5;
constexpr int kInputStyleVarCount = 2;
constexpr int kInputStyleColorCount = 3;
constexpr int kTableStyleVarCount = 1;
constexpr int kTableStyleColorCount = 5;

void pushModalStyle() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, kWindowPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kPopupRounding);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, kBorderSize);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, kPopupRounding);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, kBorderSize);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, kItemSpacing);
}

void pushTooltipStyle(const Palette& palette) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 8.0f});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kPopupRounding);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, kBorderSize);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{6.0f, 4.0f});
  ImGui::PushStyleColor(ImGuiCol_PopupBg, withAlpha(palette.bg, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(palette.border, 0.72f));
  ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
}
}  // namespace

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

ImFont* iconFont() {
  ImFontAtlas* fonts = ImGui::GetIO().Fonts;
  if (fonts && fonts->Fonts.Size > 1 && fonts->Fonts[1]) return fonts->Fonts[1];
  return ImGui::GetFont();
}

const char* tabIcon(std::string_view name) {
  if (name.find("Plot") != std::string_view::npos) return "\uea5c";
  if (name.find("Custom View") != std::string_view::npos) return "\uea7f";
  if (name.find("Arena") != std::string_view::npos) return "\uea88";
  if (name.find("Network") != std::string_view::npos) return "\uf09f";
  if (name.find("Live View") != std::string_view::npos) return "\uec6b";
  if (name.find("Dynamics") != std::string_view::npos) return "\uec4c";
  if (name.find("Dashboard") != std::string_view::npos) return "\ueab1";
  if (name.find("WIP") != std::string_view::npos) return "\uea77";
  return "\ueada";
}

ImVec2 calcIconSize(const char* icon, float size) {
  return iconFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
}

void drawIcon(ImDrawList* draw, const char* icon, ImVec2 pos, float size, ImU32 color,
              float yOffset) {
  draw->AddText(iconFont(), size, {pos.x, pos.y + yOffset}, color, icon);
}

void drawIconCentered(ImDrawList* draw, const char* icon, ImVec2 min, ImVec2 max, float size,
                      ImU32 color, float yOffset) {
  const ImVec2 iconSize = calcIconSize(icon, size);
  drawIcon(
      draw, icon,
      {min.x + (max.x - min.x - iconSize.x) * 0.5f, min.y + (max.y - min.y - iconSize.y) * 0.5f},
      size, color, yOffset);
}

Palette palette() {
  const ImGuiStyle& style = ImGui::GetStyle();
  const ImVec4 text = style.Colors[ImGuiCol_Text];
  const ImVec4 bg = style.Colors[ImGuiCol_WindowBg];
  const ImVec4 button = style.Colors[ImGuiCol_Button];
  const ImVec4 hover = style.Colors[ImGuiCol_ButtonHovered];
  const ImVec4 accent = style.Colors[ImGuiCol_NavHighlight];
  return Palette{
      .text = text,
      .muted = mixColor(text, bg, 0.48f),
      .bg = bg,
      .panel = mixColor(bg, button, 0.30f),
      .raised = mixColor(bg, button, 0.62f),
      .hover = mixColor(button, hover, 0.68f),
      .active = mixColor(button, accent, 0.38f),
      .accent = accent,
      .border = mixColor(button, text, 0.20f),
  };
}

bool beginModal(const char* title, ImVec2 size) {
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 display = io.DisplaySize;
  const ImVec2 winSize{std::min(size.x, std::max(320.0f, display.x - 64.0f)),
                       std::min(size.y, std::max(220.0f, display.y - 64.0f))};
  const ImVec2 winPos{(display.x - winSize.x) * 0.5f, (display.y - winSize.y) * 0.5f};
  ImGui::SetNextWindowSize(winSize);
  ImGui::SetNextWindowPos(winPos);
  constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
  pushModalStyle();
  const bool open = ImGui::BeginPopupModal(title, nullptr, flags);
  if (open && ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
  if (!open) ImGui::PopStyleVar(kModalStyleVarCount);
  return open;
}

void endModal(bool open) {
  if (open) {
    ImGui::EndPopup();
    ImGui::PopStyleVar(kModalStyleVarCount);
  }
}

bool modalCloseButton(const char* id, const Palette& palette, bool alignRight) {
  if (alignRight) {
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 48.0f);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 110.0f);
  }
  return button(id, "Close", {96.0f, 34.0f}, palette, false, "Close");
}

bool beginPanel(const char* id, ImVec2 size, const Palette& palette, ImGuiChildFlags childFlags,
                ImGuiWindowFlags windowFlags) {
  (void)palette;
  return ImGui::BeginChild(id, size, childFlags, windowFlags);
}

void endPanel() { ImGui::EndChild(); }

void label(std::string_view text, const Palette& palette) {
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddText(pos, colorU32(palette.muted), text.data(),
                                      text.data() + text.size());
  ImGui::Dummy(ImGui::CalcTextSize(text.data(), text.data() + text.size()));
}

void tooltip(std::string_view text, ImGuiHoveredFlags flags) {
  if (text.empty() || !ImGui::IsItemHovered(flags)) return;

  const Palette colors = palette();
  pushTooltipStyle(colors);
  ImGui::BeginTooltip();
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
  ImGui::TextUnformatted(text.data(), text.data() + text.size());
  ImGui::PopTextWrapPos();
  ImGui::EndTooltip();
  ImGui::PopStyleColor(kTooltipStyleColorCount);
  ImGui::PopStyleVar(kTooltipStyleVarCount);
}

void pushContentStyle() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, kContentWindowPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, kContentFramePadding);
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, kContentCellPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, kContentItemSpacing);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{9.0f, 7.0f});
}

void popContentStyle() { ImGui::PopStyleVar(kContentStyleVarCount); }

void pushInputStyle(const Palette& palette) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kFrameRounding);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, kContentFramePadding);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, withAlpha(palette.panel, 0.82f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, withAlpha(palette.raised, 0.88f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, withAlpha(palette.raised, 0.96f));
}

void popInputStyle() {
  ImGui::PopStyleColor(kInputStyleColorCount);
  ImGui::PopStyleVar(kInputStyleVarCount);
}

void pushTableStyle(const Palette& palette) {
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, kContentCellPadding);
  ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, withAlpha(palette.raised, 0.76f));
  ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, withAlpha(palette.border, 0.56f));
  ImGui::PushStyleColor(ImGuiCol_TableBorderLight, withAlpha(palette.border, 0.42f));
  ImGui::PushStyleColor(ImGuiCol_TableRowBg, withAlpha(palette.panel, 0.46f));
  ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, withAlpha(palette.raised, 0.34f));
}

void popTableStyle() {
  ImGui::PopStyleColor(kTableStyleColorCount);
  ImGui::PopStyleVar(kTableStyleVarCount);
}

ControlState control(const char* id, ImVec2 size, bool selected, float hoverFocus,
                     float activeFocus, float focusSeconds, float pressSeconds) {
  ImGui::InvisibleButton(id, size);
  ControlState state{};
  state.clicked = ImGui::IsItemClicked();
  state.hovered = ImGui::IsItemHovered();
  state.active = ImGui::IsItemActive();
  state.id = ImGui::GetItemID();
  state.min = ImGui::GetItemRectMin();
  state.max = ImGui::GetItemRectMax();
  const float target = selected        ? 1.0f
                       : state.active  ? activeFocus
                       : state.hovered ? hoverFocus
                                       : 0.0f;
  state.focus = iam_tween_float(state.id, ImHashStr("focus"), target, focusSeconds,
                                iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade,
                                ImGui::GetIO().DeltaTime, selected ? 1.0f : 0.0f);
  state.press = iam_tween_float(state.id, ImHashStr("press"), state.active ? 1.0f : 0.0f,
                                pressSeconds, iam_ease_preset(iam_ease_out_quad),
                                iam_policy_crossfade, ImGui::GetIO().DeltaTime);
  return state;
}

bool button(const char* id, std::string_view text, ImVec2 size, const Palette& palette,
            bool selected, std::string_view tooltipText) {
  ImGui::PushID(id);
  const ControlState state = control("button", size, selected);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(
      {state.min.x + state.press, state.min.y + state.press},
      {state.max.x - state.press, state.max.y + state.press},
      colorU32(withAlpha(mixColor(palette.raised, palette.active, state.focus), 0.88f)), 8.0f);
  draw->AddRect({state.min.x + state.press, state.min.y + state.press},
                {state.max.x - state.press, state.max.y + state.press},
                colorU32(withAlpha(palette.border, 0.38f + state.focus * 0.28f)), 8.0f);
  const ImVec2 textSize = ImGui::CalcTextSize(text.data(), text.data() + text.size());
  draw->AddText(
      {state.min.x + (size.x - textSize.x) * 0.5f,
       state.min.y + (size.y - textSize.y) * 0.5f + state.press},
      colorU32(selected ? palette.text : mixColor(palette.muted, palette.text, state.focus)),
      text.data(), text.data() + text.size());
  tooltip(tooltipText);
  ImGui::PopID();
  return state.clicked;
}

bool iconButton(const char* id, const char* icon, std::string_view tooltipText, ImVec2 size,
                const Palette& palette, bool selected, float iconSize) {
  ImGui::PushID(id);
  const ControlState state = control("icon", size, selected, 0.62f, 1.0f);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec4 fill = mixColor(palette.raised, palette.active, state.focus);
  draw->AddRectFilled(state.min, state.max, colorU32(withAlpha(fill, 0.88f)), 8.0f);
  draw->AddRect(state.min, state.max,
                colorU32(withAlpha(palette.border, 0.42f + state.focus * 0.24f)), 8.0f);
  const ImVec4 iconColor = mixColor(palette.muted, palette.text, 0.35f + state.focus * 0.65f);
  drawIconCentered(draw, icon, state.min, state.max, iconSize, colorU32(iconColor), 1.0f);
  tooltip(tooltipText);
  ImGui::PopID();
  return state.clicked;
}

bool rowButton(const char* id, const char* icon, std::string_view text, ImVec2 size,
               const Palette& palette, bool selected, bool disabled, bool transparent,
               float textSize, float iconSize) {
  ImGui::PushID(id);
  if (disabled) ImGui::BeginDisabled();
  const ControlState state = control("row", size, selected, 0.62f, 1.0f);
  if (disabled) ImGui::EndDisabled();

  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec4 fill = disabled
                          ? withAlpha(palette.panel, 0.55f)
                          : withAlpha(mixColor(palette.raised, palette.active, state.focus), 0.88f);
  const ImVec4 fg = disabled   ? withAlpha(palette.muted, 0.65f)
                    : selected ? palette.text
                               : mixColor(palette.muted, palette.text, 0.48f + state.focus * 0.52f);
  if (!transparent) {
    draw->AddRectFilled(state.min, state.max, colorU32(fill), 8.0f);
    draw->AddRect(state.min, state.max,
                  colorU32(withAlpha(palette.border, 0.40f + state.focus * 0.24f)), 8.0f);
  }
  const bool hasIcon = icon && icon[0] != '\0';
  const bool customSizing = textSize > 0.0f;
  ImFont* font = ImGui::GetFont();
  textSize = textSize > 0.0f ? textSize : ImGui::GetFontSize();
  const ImVec2 labelSize =
      font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, text.data(), text.data() + text.size());
  const float contentX =
      customSizing ? state.min.x + (size.x - size.y - 4.0f - labelSize.x) * 0.5f : state.min.x;
  if (hasIcon)
    drawIconCentered(
        draw, icon,
        customSizing ? ImVec2{contentX, state.min.y} : ImVec2{state.min.x + 8.0f, state.min.y},
        customSizing ? ImVec2{contentX + size.y, state.max.y}
                     : ImVec2{state.min.x + 32.0f, state.max.y},
        iconSize, colorU32(fg), 1.0f);
  const float textX =
      hasIcon ? (customSizing ? contentX + size.y + 4.0f : state.min.x + 38.0f)
              : (customSizing ? state.min.x + (size.x - labelSize.x) * 0.5f : state.min.x + 12.0f);
  draw->PushClipRect({textX, state.min.y}, {state.max.x - 10.0f, state.max.y}, true);
  draw->AddText(font, textSize, {textX, state.min.y + (size.y - labelSize.y) * 0.5f}, colorU32(fg),
                text.data(), text.data() + text.size());
  draw->PopClipRect();
  tooltip(text);
  ImGui::PopID();
  return state.clicked && !disabled;
}

void infoPanel(const char* id, std::string_view heading, std::string_view body, ImVec2 size,
               const Palette& palette) {
  ImGui::PushID(id);
  ImGui::InvisibleButton("panel", size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(min, max, colorU32(withAlpha(palette.panel, 0.76f)), 8.0f);
  draw->AddRect(min, max, colorU32(withAlpha(palette.border, 0.48f)), 8.0f);
  draw->PushClipRect({min.x + 14.0f, min.y + 10.0f}, {max.x - 14.0f, max.y - 10.0f}, true);
  draw->AddText({min.x + 14.0f, min.y + 12.0f}, colorU32(palette.text), heading.data(),
                heading.data() + heading.size());
  draw->AddText({min.x + 14.0f, min.y + 39.0f}, colorU32(palette.muted), body.data(),
                body.data() + body.size());
  draw->PopClipRect();
  ImGui::PopID();
}

}  // namespace PhotonUi
