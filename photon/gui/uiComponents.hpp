#pragma once

#include <string_view>

#include "imgui.h"

namespace PhotonUi {

struct Palette {
  ImVec4 text;
  ImVec4 muted;
  ImVec4 bg;
  ImVec4 panel;
  ImVec4 raised;
  ImVec4 hover;
  ImVec4 active;
  ImVec4 accent;
  ImVec4 border;
};

Palette palette();
ImVec4 withAlpha(ImVec4 color, float alpha);
ImVec4 mixColor(ImVec4 a, ImVec4 b, float t);
ImU32 colorU32(ImVec4 color);
const char* tabIcon(std::string_view name);

bool beginModal(const char* title, ImVec2 size);
void endModal(bool open);
bool beginPanel(const char* id, ImVec2 size, const Palette& palette,
                ImGuiChildFlags childFlags = ImGuiChildFlags_Borders,
                ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoSavedSettings);
void endPanel();
void label(std::string_view text, const Palette& palette);
void tooltip(std::string_view text, ImGuiHoveredFlags flags = ImGuiHoveredFlags_DelayShort);
bool button(const char* id, std::string_view text, ImVec2 size, const Palette& palette,
            bool selected = false, std::string_view tooltipText = {});
void leftAccentFrame(ImVec2 min, ImVec2 max, ImU32 color, float rounding, float width);
void infoPanel(const char* id, std::string_view heading, std::string_view body, ImVec2 size,
               const Palette& palette);

}  // namespace PhotonUi
