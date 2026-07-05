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

struct ControlState {
  bool clicked = false;
  bool hovered = false;
  bool active = false;
  ImGuiID id = 0;
  ImVec2 min{};
  ImVec2 max{};
  float focus = 0.0f;
  float press = 0.0f;
};

Palette palette();
ImVec4 withAlpha(ImVec4 color, float alpha);
ImVec4 mixColor(ImVec4 a, ImVec4 b, float t);
ImU32 colorU32(ImVec4 color);
ImFont* iconFont();
const char* tabIcon(std::string_view name);
ImVec2 calcIconSize(const char* icon, float size);
void drawIcon(ImDrawList* draw, const char* icon, ImVec2 pos, float size, ImU32 color,
              float yOffset = 0.0f);
void drawIconCentered(ImDrawList* draw, const char* icon, ImVec2 min, ImVec2 max, float size,
                      ImU32 color, float yOffset = 0.0f);

bool beginModal(const char* title, ImVec2 size);
void endModal(bool open);
bool beginPanel(const char* id, ImVec2 size, const Palette& palette,
                ImGuiChildFlags childFlags = ImGuiChildFlags_Borders,
                ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoSavedSettings);
void endPanel();
void label(std::string_view text, const Palette& palette);
void tooltip(std::string_view text, ImGuiHoveredFlags flags = ImGuiHoveredFlags_DelayShort);
ControlState control(const char* id, ImVec2 size, bool selected = false, float hoverFocus = 0.58f,
                     float activeFocus = 0.88f, float focusSeconds = 0.14f,
                     float pressSeconds = 0.08f);
bool button(const char* id, std::string_view text, ImVec2 size, const Palette& palette,
            bool selected = false, std::string_view tooltipText = {});
bool iconButton(const char* id, const char* icon, std::string_view tooltipText, ImVec2 size,
                const Palette& palette, bool selected = false);
bool rowButton(const char* id, const char* icon, std::string_view text, ImVec2 size,
               const Palette& palette, bool selected = false, bool disabled = false);
void pushInputStyle(const Palette& palette);
void popInputStyle();
void leftAccentFrame(ImVec2 min, ImVec2 max, ImU32 color, float rounding, float width);
void infoPanel(const char* id, std::string_view heading, std::string_view body, ImVec2 size,
               const Palette& palette);

}  // namespace PhotonUi
