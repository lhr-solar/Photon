#include "uiComponents.hpp"

#include <algorithm>

#include "im_anim.h"
#include "imgui_internal.h"

namespace PhotonUi {

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

const char* tabIcon(std::string_view name) {
  if (name.find("Plot") != std::string_view::npos) return "\uE6E1";
  if (name.find("Arena") != std::string_view::npos) return "\uE875";
  if (name.find("Network") != std::string_view::npos) return "\uE640";
  if (name.find("Shader") != std::string_view::npos) return "\uE3B7";
  return "\uE5C3";
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
  ImGui::SetNextWindowSize(winSize, ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(winPos, ImGuiCond_Appearing);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 14.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
  const Palette p = palette();
  ImGui::PushStyleColor(ImGuiCol_PopupBg, withAlpha(p.bg, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, withAlpha(p.bg, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(p.border, 0.70f));
  constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoDocking |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoScrollbar;
  return ImGui::BeginPopupModal(title, nullptr, flags);
}

void endModal(bool open) {
  if (open) ImGui::EndPopup();
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(4);
}

bool beginPanel(const char* id, ImVec2 size, const Palette& palette, ImGuiChildFlags childFlags,
                ImGuiWindowFlags windowFlags) {
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, withAlpha(palette.panel, 0.76f));
  ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(palette.border, 0.48f));
  return ImGui::BeginChild(id, size, childFlags, windowFlags);
}

void endPanel() {
  ImGui::EndChild();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}

void label(std::string_view text, const Palette& palette) {
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddText(pos, colorU32(palette.muted), text.data(),
                                      text.data() + text.size());
  ImGui::Dummy(ImGui::CalcTextSize(text.data(), text.data() + text.size()));
}

bool button(const char* id, std::string_view text, ImVec2 size, const Palette& palette,
            bool selected) {
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
  const ImVec2 textSize = ImGui::CalcTextSize(text.data(), text.data() + text.size());
  draw->AddText(
      {min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f + press},
      colorU32(selected ? palette.text : mixColor(palette.muted, palette.text, focus)), text.data(),
      text.data() + text.size());
  ImGui::PopID();
  return clicked;
}

void leftAccentFrame(ImVec2 min, ImVec2 max, ImU32 color, float rounding, float width) {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(min, {min.x + width, max.y}, true);
  ImGui::RenderFrame(min, max, color, false, rounding);
  draw->PopClipRect();
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
