#pragma once

#include <string_view>

#include "imgui.h"

namespace PhotonUi {

inline constexpr float kFrameRounding = 8.0f;
inline constexpr float kPanelRounding = 8.0f;
inline constexpr float kPopupRounding = 10.0f;
inline constexpr float kScrollbarRounding = 999.0f;
inline constexpr float kScrollbarSize = 10.0f;
inline constexpr float kBorderSize = 1.0f;
inline constexpr ImVec2 kWindowPadding{14.0f, 12.0f};
inline constexpr ImVec2 kFramePadding{10.0f, 7.0f};
inline constexpr ImVec2 kCellPadding{12.0f, 7.0f};
inline constexpr ImVec2 kItemSpacing{10.0f, 8.0f};
inline constexpr ImVec2 kContentWindowPadding{16.0f, 14.0f};
inline constexpr ImVec2 kContentFramePadding{12.0f, 9.0f};
inline constexpr ImVec2 kContentCellPadding{14.0f, 8.0f};
inline constexpr ImVec2 kContentItemSpacing{12.0f, 10.0f};

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
bool modalCloseButton(const char* id, const Palette& palette, bool alignRight = true);
bool beginPanel(const char* id, ImVec2 size, const Palette& palette,
                ImGuiChildFlags childFlags = ImGuiChildFlags_Borders,
                ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoSavedSettings);
void endPanel();
void label(std::string_view text, const Palette& palette);
void tooltip(std::string_view text, ImGuiHoveredFlags flags = ImGuiHoveredFlags_DelayShort);
void pushContentStyle();
void popContentStyle();
void pushInputStyle(const Palette& palette);
void popInputStyle();
void pushTableStyle(const Palette& palette);
void popTableStyle();
ControlState control(const char* id, ImVec2 size, bool selected = false, float hoverFocus = 0.58f,
                     float activeFocus = 0.88f, float focusSeconds = 0.14f,
                     float pressSeconds = 0.08f);
bool button(const char* id, std::string_view text, ImVec2 size, const Palette& palette,
            bool selected = false, std::string_view tooltipText = {});
bool iconButton(const char* id, const char* icon, std::string_view tooltipText, ImVec2 size,
                const Palette& palette, bool selected = false);
bool rowButton(const char* id, const char* icon, std::string_view text, ImVec2 size,
               const Palette& palette, bool selected = false, bool disabled = false,
               bool transparent = false);
void infoPanel(const char* id, std::string_view heading, std::string_view body, ImVec2 size,
               const Palette& palette);

}  // namespace PhotonUi
