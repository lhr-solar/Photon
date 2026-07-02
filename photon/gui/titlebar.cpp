#include "titlebar.hpp"

#include <algorithm>

#include "SDL3/SDL.h"
#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"

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

struct TitleButtonResult {
  bool clicked = false;
  ImVec2 min{};
  ImVec2 max{};
  float focus = 0.0f;
  float press = 0.0f;
};

TitleButtonResult titleButton(const char* id, ImVec2 size, bool selected, ImVec4 accent) {
  ImGui::PushID(id);
  ImGui::InvisibleButton("button", size);
  TitleButtonResult result{};
  result.clicked = ImGui::IsItemClicked();
  result.min = ImGui::GetItemRectMin();
  result.max = ImGui::GetItemRectMax();
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  const float dt = ImGui::GetIO().DeltaTime;
  const ImGuiID itemId = ImGui::GetItemID();
  result.focus = iam_tween_float(itemId, ImHashStr("focus"),
                                 selected  ? 1.0f
                                 : active  ? 0.86f
                                 : hovered ? 0.58f
                                           : 0.0f,
                                 0.16f, iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade,
                                 dt, selected ? 1.0f : 0.0f);
  result.press = iam_tween_float(itemId, ImHashStr("press"), active ? 1.0f : 0.0f, 0.08f,
                                 iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade, dt);

  const ImGuiStyle& style = ImGui::GetStyle();
  const ImVec4 bg = style.Colors[ImGuiCol_WindowBg];
  const ImVec4 button = style.Colors[ImGuiCol_Button];
  const ImVec4 fill = mixColor(withAlpha(button, 0.0f), accent, result.focus);
  const float inset = 5.0f + result.press * 1.5f;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  if (selected || result.focus > 0.03f) {
    draw->AddRectFilled({result.min.x + inset, result.min.y + 5.0f + result.press},
                        {result.max.x - inset, result.max.y - 5.0f + result.press},
                        colorU32(withAlpha(mixColor(bg, fill, 0.68f), 0.88f)), 8.0f);
  }
  if (result.focus > 0.04f) {
    draw->AddRect({result.min.x + inset, result.min.y + 5.0f + result.press},
                  {result.max.x - inset, result.max.y - 5.0f + result.press},
                  colorU32(withAlpha(accent, 0.34f + result.focus * 0.28f)), 8.0f);
  }
  ImGui::PopID();
  return result;
}

void drawSidebarToggleIcon(ImDrawList* draw, const TitleButtonResult& button, bool open,
                           ImVec4 textColor, ImVec4 accent) {
  const float dt = ImGui::GetIO().DeltaTime;
  const float openT = iam_tween_float(ImHashStr("TitleSidebarToggle"), ImHashStr("open"),
                                      open ? 1.0f : 0.0f, 0.20f, iam_ease_preset(iam_ease_out_quad),
                                      iam_policy_crossfade, dt, open ? 1.0f : 0.0f);
  const ImVec4 icon = mixColor(textColor, accent, 0.25f + button.focus * 0.45f);
  const ImU32 iconColor = colorU32(icon);
  const float pressY = button.press;
  const float w = button.max.x - button.min.x;
  const float h = button.max.y - button.min.y;
  const float boxW = 17.0f;
  const float boxH = 13.0f;
  const ImVec2 boxMin(button.min.x + (w - boxW) * 0.5f, button.min.y + (h - boxH) * 0.5f + pressY);
  const ImVec2 boxMax(boxMin.x + boxW, boxMin.y + boxH);
  draw->AddRect(boxMin, boxMax, colorU32(withAlpha(icon, 0.78f)), 3.0f, 0, 1.35f);
  const float paneInset = 2.25f;
  const float paneW = 4.2f + openT * 1.6f;
  draw->AddRectFilled({boxMin.x + paneInset, boxMin.y + paneInset},
                      {boxMin.x + paneInset + paneW, boxMax.y - paneInset},
                      colorU32(withAlpha(accent, 0.42f + openT * 0.22f)), 2.0f);
  const float lineX = boxMin.x + 9.5f - openT * 1.3f;
  draw->AddLine({lineX, boxMin.y + 4.4f}, {boxMax.x - 3.2f, boxMin.y + 4.4f}, iconColor, 1.25f);
  draw->AddLine({lineX, boxMin.y + 8.0f}, {boxMax.x - 4.4f, boxMin.y + 8.0f}, iconColor, 1.25f);
}

void drawWindowIcon(ImDrawList* draw, const TitleButtonResult& button, WindowAction action,
                    ImVec4 textColor) {
  const ImU32 iconColor = colorU32(withAlpha(textColor, 0.78f + button.focus * 0.22f));
  const float w = button.max.x - button.min.x;
  const float h = button.max.y - button.min.y;
  const float s = 10.0f;
  const ImVec2 p(button.min.x + (w - s) * 0.5f, button.min.y + (h - s) * 0.5f + button.press);
  if (action == WindowAction::Minimize) {
    draw->AddLine({p.x, p.y + s}, {p.x + s, p.y + s}, iconColor, 1.65f);
  } else if (action == WindowAction::ToggleMaximize) {
    draw->AddRect({p.x + 1.0f, p.y + 1.0f}, {p.x + s - 1.0f, p.y + s - 1.0f}, iconColor, 0.0f, 0,
                  1.45f);
  } else if (action == WindowAction::Close) {
    const float inset = 1.0f;
    const float center = s * 0.5f;
    const ImVec2 c(p.x + center, p.y + center);
    draw->AddLine({p.x + inset, p.y + inset}, c, iconColor, 1.65f);
    draw->AddLine(c, {p.x + s - inset, p.y + s - inset}, iconColor, 1.65f);
    draw->AddLine({p.x + inset, p.y + s - inset}, c, iconColor, 1.65f);
    draw->AddLine(c, {p.x + s - inset, p.y + inset}, iconColor, 1.65f);
  }
}
}  // namespace

void TitleBar::clearInteract() {
  interactiveRectCount = 0;
  for (SDL_Rect& rect : interactiveRects) rect = SDL_Rect{0, 0, 0, 0};
}

void TitleBar::addInteract(const ImVec2& min, const ImVec2& max) {
  if (interactiveRectCount >= buttonCount) return;
  SDL_Rect& rect = interactiveRects[interactiveRectCount++];
  rect.x = static_cast<int>(min.x);
  rect.y = static_cast<int>(min.y);
  rect.w = static_cast<int>(max.x - min.x);
  rect.h = static_cast<int>(max.y - min.y);
}

bool TitleBar::isInteract(int x, int y) const {
  for (int i = 0; i < interactiveRectCount; ++i) {
    const SDL_Rect& rect = interactiveRects[i];
    if ((x >= rect.x) && (x < rect.x + rect.w) && (y >= rect.y) && (y < rect.y + rect.h))
      return true;
  }
  return false;
}

void TitleBar::draw() {
  clearInteract();
  if (!enabled) return;
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImGuiStyle& style = ImGui::GetStyle();
  const float barHeight = static_cast<float>(height);
  const ImVec2 pos = viewport->Pos;
  const ImVec2 size(viewport->Size.x, barHeight);
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGui::SetNextWindowViewport(viewport->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  const ImVec4 windowBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(windowBg.x, windowBg.y, windowBg.z, 0.85f));
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (ImGui::Begin("##PhotonTitleBar", nullptr, flags)) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const ImVec4 accentColor = ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight);
    const float buttonWidth = barHeight;

    TitleButtonResult sidebarButton =
        titleButton("toggleSidebar", ImVec2(buttonWidth, barHeight), showSidebar, accentColor);
    if (sidebarButton.clicked) showSidebar = !showSidebar;
    addInteract(sidebarButton.min, sidebarButton.max);
    drawSidebarToggleIcon(draw, sidebarButton, showSidebar, textColor, accentColor);

    const float controlsWidth = buttonWidth * 3.0f;
    const float controlsX = ImGui::GetWindowWidth() - controlsWidth - 6.0f;

    ImGui::SetCursorPos(ImVec2(controlsX, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, style.ItemSpacing.y));

    TitleButtonResult minimizeButton =
        titleButton("minimize", ImVec2(buttonWidth, barHeight), false, accentColor);
    if (minimizeButton.clicked) pendingAction = WindowAction::Minimize;
    drawWindowIcon(draw, minimizeButton, WindowAction::Minimize, textColor);
    addInteract(minimizeButton.min, minimizeButton.max);

    ImGui::SameLine();
    TitleButtonResult maximizeButton =
        titleButton("maximize", ImVec2(buttonWidth, barHeight), false, accentColor);
    if (maximizeButton.clicked) pendingAction = WindowAction::ToggleMaximize;
    drawWindowIcon(draw, maximizeButton, WindowAction::ToggleMaximize, textColor);
    addInteract(maximizeButton.min, maximizeButton.max);

    ImGui::SameLine();
    TitleButtonResult closeButton =
        titleButton("exit", ImVec2(buttonWidth, barHeight), false, accentColor);
    if (closeButton.clicked) pendingAction = WindowAction::Close;
    drawWindowIcon(draw, closeButton, WindowAction::Close, textColor);
    addInteract(closeButton.min, closeButton.max);

    ImGui::PopStyleVar();
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);

  switch (pendingAction) {
    case WindowAction::Close: {
      SDL_Event quitEvent{};
      quitEvent.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quitEvent);
      break;
    }
    case WindowAction::Minimize:
      SDL_MinimizeWindow(window);
      break;
    case WindowAction::ToggleMaximize:
      if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0)
        SDL_RestoreWindow(window);
      else
        SDL_MaximizeWindow(window);
      break;
    case WindowAction::None:
      break;
  }
  pendingAction = WindowAction::None;
}
