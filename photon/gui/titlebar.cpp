#include "titlebar.hpp"

#include "SDL3/SDL.h"
#include "imgui.h"

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
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (ImGui::Begin("##PhotonTitleBar", nullptr, flags)) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 color = IM_COL32(255, 255, 255, 255);
    const ImU32 buttonColor = ImGui::GetColorU32(ImGuiCol_Button);
    const float buttonWidth = barHeight;

    if (ImGui::Button("##toggleSidebar", ImVec2(buttonWidth, barHeight)))
      showSidebar = !showSidebar;
    addInteract(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    float s = max.x * 0.2f;
    ImVec2 p = ImVec2((min.x + max.x - s) * 0.5f, (min.y + max.y - s) * 0.5f);
    draw->AddLine({p.x, p.y}, {p.x + s, p.y}, IM_COL32(255, 255, 255, 255), 1.0f);
    draw->AddLine({p.x, p.y + (s * 0.4f)}, {p.x + s, p.y + (s * 0.4f)},
                  IM_COL32(255, 255, 255, 255), 1.0f);
    draw->AddLine({p.x, p.y + (s * 0.8f)}, {p.x + s, p.y + (s * 0.8f)},
                  IM_COL32(255, 255, 255, 255), 1.0f);

    ImGui::SetCursorPos(
        ImVec2(ImGui::GetWindowWidth() - buttonWidth * static_cast<float>(3), 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, style.ItemSpacing.y));

    if (ImGui::Button("##minimize", ImVec2(buttonWidth, barHeight)))
      pendingAction = WindowAction::Minimize;
    min = ImGui::GetItemRectMin();
    max = ImGui::GetItemRectMax();
    s = s;
    p = ImVec2((min.x + max.x - s) * 0.5f, (min.y + max.y - s) * 0.5f);
    draw->AddLine({p.x, p.y + s}, {p.x + s, p.y + s}, color, 2.0f);
    addInteract(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    s = s / 2.0f;
    ImGui::SameLine();
    if (ImGui::Button("##maximize", ImVec2(buttonWidth, barHeight)))
      pendingAction = WindowAction::ToggleMaximize;
    min = ImGui::GetItemRectMin();
    max = ImGui::GetItemRectMax();
    p = ImVec2((min.x + max.x - 3.0f * s) * 0.5f, (min.y + max.y - 3.0f * s) * 0.5f);
    draw->AddRect({p.x + s, p.y}, {p.x + 3.0f * s, p.y + 2.0f * s}, color);
    draw->AddRectFilled({p.x, p.y + s}, {p.x + 2.0f * s, p.y + 3.0f * s}, buttonColor);
    draw->AddRect({p.x, p.y + s}, {p.x + 2.0f * s, p.y + 3.0f * s}, color);
    addInteract(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    s = s * 2.0f;
    ImGui::SameLine();
    if (ImGui::Button("##exit", ImVec2(buttonWidth, barHeight)))
      pendingAction = WindowAction::Close;
    min = ImGui::GetItemRectMin();
    max = ImGui::GetItemRectMax();
    p = ImVec2((min.x + max.x - s) * 0.5f, (min.y + max.y - s) * 0.5f);
    draw->AddLine(p, {p.x + s, p.y + s}, color, 2.0f);
    draw->AddLine({p.x, p.y + s}, {p.x + s, p.y}, color, 2.0f);
    addInteract(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    ImGui::PopStyleVar();
  }
  ImGui::End();
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
