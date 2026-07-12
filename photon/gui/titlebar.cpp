#include "titlebar.hpp"

#include <algorithm>
#include <cfloat>
#include <string_view>

#include "SDL3/SDL.h"
#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "uiComponents.hpp"

namespace {
using PhotonUi::colorU32;
using PhotonUi::mixColor;
using PhotonUi::withAlpha;

struct TitleButtonResult {
  bool clicked = false;
  ImVec2 min{};
  ImVec2 max{};
  float focus = 0.0f;
  float press = 0.0f;
};

TitleButtonResult titleButton(const char* id, ImVec2 size, bool selected, ImVec4 accent) {
  ImGui::PushID(id);
  const PhotonUi::ControlState state =
      PhotonUi::control("button", size, selected, 0.58f, 0.86f, 0.16f);
  TitleButtonResult result{
      .clicked = state.clicked,
      .min = state.min,
      .max = state.max,
      .focus = state.focus,
      .press = state.press,
  };

  const ImGuiStyle& style = ImGui::GetStyle();
  const ImVec4 bg = style.Colors[ImGuiCol_WindowBg];
  const ImVec4 button = style.Colors[ImGuiCol_Button];
  const ImVec4 fill = mixColor(withAlpha(button, 0.0f), accent, result.focus);
  const float buttonHeight = result.max.y - result.min.y;
  const float padX = std::max(1.0f, buttonHeight * 0.13f);
  const float padY = std::max(1.0f, buttonHeight * 0.10f);
  const float rounding = std::max(1.0f, buttonHeight * 0.20f);
  const float inset = padX + result.press;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  if (selected || result.focus > 0.03f) {
    draw->AddRectFilled({result.min.x + inset, result.min.y + padY + result.press},
                        {result.max.x - inset, result.max.y - padY + result.press},
                        colorU32(withAlpha(mixColor(bg, fill, 0.68f), 0.88f)), rounding);
  }
  if (result.focus > 0.04f) {
    draw->AddRect({result.min.x + inset, result.min.y + padY + result.press},
                  {result.max.x - inset, result.max.y - padY + result.press},
                  colorU32(withAlpha(accent, 0.34f + result.focus * 0.28f)), rounding);
  }
  ImGui::PopID();
  return result;
}

void drawSidebarToggleIcon(ImDrawList* draw, const TitleButtonResult& button, bool open,
                           ImVec4 textColor, ImVec4 accent) {
  const ImVec4 icon = mixColor(textColor, accent, 0.25f + button.focus * 0.45f);
  const float iconSize = (button.max.y - button.min.y) * 0.47f;
  PhotonUi::drawIconCentered(draw, open ? "\ueada" : "\ufd47", button.min, button.max, iconSize,
                             colorU32(icon), button.press + 1.0f);
}

void drawWindowIcon(ImDrawList* draw, const TitleButtonResult& button, WindowAction action,
                    ImVec4 textColor, bool windowMaximized = false) {
  const ImU32 iconColor = colorU32(withAlpha(textColor, 0.78f + button.focus * 0.22f));
  const char* icon = "\ueaf2";
  const float buttonHeight = button.max.y - button.min.y;
  float iconSize = buttonHeight * 0.43f;
  if (action == WindowAction::Minimize) {
    icon = "\ueaf2";
  } else if (action == WindowAction::ToggleMaximize) {
    icon = windowMaximized ? "\uf15f" : "\ueaea";
    iconSize = buttonHeight * 0.40f;
  } else if (action == WindowAction::Close) {
    icon = "\ueb55";
  }
  PhotonUi::drawIconCentered(draw, icon, button.min, button.max, iconSize, iconColor,
                             button.press + 1.0f);
}

void drawCollapsedSidebarHeader(ImDrawList* draw, ImVec2 min, ImVec2 max, std::string_view page,
                                float alpha) {
  if (alpha <= 0.01f || max.x <= min.x) return;
  const PhotonUi::Palette palette = PhotonUi::palette();
  const ImVec4 headerText = withAlpha(palette.muted, palette.muted.w * alpha);
  ImFont* font = ImGui::GetFont();
  const char* icon = PhotonUi::tabIcon(page);
  const float height = max.y - min.y;
  const float iconBox = height;
  const float iconSize = height * 0.47f;
  const float labelSize = std::min(ImGui::GetFontSize(), height * 0.46f);
  const ImVec2 iconMin(min.x, min.y);
  const ImVec2 iconMax(iconMin.x + iconBox, iconMin.y + iconBox);
  PhotonUi::drawIconCentered(draw, icon, iconMin, iconMax, iconSize, colorU32(headerText), 1.0f);

  const float textX = iconMax.x + 4.0f;
  if (textX >= max.x) return;

  draw->PushClipRect({textX, min.y}, max, true);
  const ImVec2 pageSize =
      font->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, page.data(), page.data() + page.size());
  draw->AddText(font, labelSize, {textX, min.y + (max.y - min.y - pageSize.y) * 0.5f},
                colorU32(headerText), page.data(), page.data() + page.size());
  draw->PopClipRect();
}

void drawConnectionStatus(ImDrawList* draw, ImVec2 min, ImVec2 max, std::string_view protocol,
                          bool active, bool connected, ImVec4 textColor, ImVec4 accent) {
  const ImGuiStyle& style = ImGui::GetStyle();
  const ImVec4 bg = style.Colors[ImGuiCol_WindowBg];
  const ImVec4 button = style.Colors[ImGuiCol_Button];
  const ImVec4 border = mixColor(button, textColor, 0.22f);
  const float height = max.y - min.y;
  const float rounding = std::max(1.0f, height * 0.28f);
  const float padY = std::max(1.0f, height * 0.17f);
  const ImVec2 frameMin{min.x, min.y + padY};
  const ImVec2 frameMax{max.x, max.y - padY};

  draw->AddRectFilled(frameMin, frameMax, colorU32(withAlpha(mixColor(bg, button, 0.42f), 0.72f)),
                      rounding);
  draw->AddRect(frameMin, frameMax, colorU32(withAlpha(border, 0.38f)), rounding);

  const ImVec4 connectedColor{0.22f, 0.86f, 0.46f, 1.0f};
  const ImVec4 pendingColor{1.0f, 0.68f, 0.25f, 1.0f};
  const ImVec4 dotColor = connected ? connectedColor
                          : active  ? pendingColor
                                    : withAlpha(textColor, 0.34f);
  const ImVec4 labelColor = connected ? mixColor(textColor, accent, 0.18f)
                            : active  ? mixColor(textColor, pendingColor, 0.24f)
                                      : withAlpha(textColor, 0.58f);
  const std::string_view label =
      (active && !protocol.empty()) ? protocol : std::string_view{"Offline"};
  const float dotRadius = std::max(2.0f, height * 0.115f);
  const float textSize = std::min(ImGui::GetFontSize(), height * 0.42f);
  const float dotX = frameMin.x + 12.0f;
  const float centerY = (frameMin.y + frameMax.y) * 0.5f;
  draw->AddCircleFilled({dotX, centerY}, dotRadius, colorU32(dotColor), 16);

  const float textX = dotX + dotRadius + 7.0f;
  draw->PushClipRect({textX, frameMin.y}, {frameMax.x - 8.0f, frameMax.y}, true);
  const ImVec2 labelSize = ImGui::GetFont()->CalcTextSizeA(textSize, FLT_MAX, 0.0f, label.data(),
                                                           label.data() + label.size());
  draw->AddText(ImGui::GetFont(), textSize, {textX, centerY - labelSize.y * 0.5f},
                colorU32(labelColor), label.data(), label.data() + label.size());
  draw->PopClipRect();
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
    const float statusWidth = 104.0f;
    const float statusX = controlsX - statusWidth - 8.0f;
    const float headerRight = statusX > sidebarButton.max.x + 24.0f ? statusX : controlsX;
    const float collapsedHeaderAlpha =
        iam_tween_float(ImHashStr("TitleCollapsedSidebarHeader"), ImHashStr("alpha"),
                        showSidebar ? 0.0f : 1.0f, 0.18f, iam_ease_preset(iam_ease_out_quad),
                        iam_policy_crossfade, ImGui::GetIO().DeltaTime, showSidebar ? 0.0f : 1.0f);
    drawCollapsedSidebarHeader(draw, {sidebarButton.max.x + 4.0f, 0.0f},
                               {headerRight - 10.0f, barHeight}, activePage, collapsedHeaderAlpha);

    if (statusX > sidebarButton.max.x + 24.0f) {
      drawConnectionStatus(draw, {statusX, 0.0f}, {statusX + statusWidth, barHeight},
                           connectionProtocol, connectionActive, connectionConnected, textColor,
                           accentColor);
    }

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
    drawWindowIcon(draw, maximizeButton, WindowAction::ToggleMaximize, textColor,
                   (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0);
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
