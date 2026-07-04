#include "sideBar.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <iterator>
#include <string_view>
#include <utility>

#include "background_jpg.hpp"
#include "gui.hpp"
#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "tabs.hpp"
#include "titlebar.hpp"
#include "uiComponents.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {
constexpr SDL_DialogFileFilter kDBCFileFilters[] = {
    {"DBC files", "dbc"},
    {"All files", "*"},
};

using SidebarPalette = PhotonUi::Palette;
using PhotonUi::colorU32;
using PhotonUi::mixColor;
using PhotonUi::withAlpha;

float smoothstep(float edge0, float edge1, float x) {
  const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float roundedRectDistance(ImVec2 p, ImVec2 center, ImVec2 halfSize, float radius) {
  const ImVec2 q(std::abs(p.x - center.x) - (halfSize.x - radius),
                 std::abs(p.y - center.y) - (halfSize.y - radius));
  const float outsideX = std::max(q.x, 0.0f);
  const float outsideY = std::max(q.y, 0.0f);
  return std::sqrt(outsideX * outsideX + outsideY * outsideY) + std::min(std::max(q.x, q.y), 0.0f) -
         radius;
}

void drawNotificationField(ImDrawList* draw, ImVec2 min, ImVec2 max, float focus, float press) {
  const ImVec2 size(max.x - min.x, max.y - min.y);
  const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  const ImVec2 half(size.x * 0.5f, size.y * 0.5f);
  const float radius = 8.0f;
  const float extent = 31.0f + press * 8.0f;
  const float step = 2.5f;
  const float time = static_cast<float>(ImGui::GetTime());
  const float gain = 1.10f + focus * 0.38f + press * 0.58f;

  for (float y = min.y - extent; y < max.y + extent; y += step) {
    for (float x = min.x - extent; x < max.x + extent; x += step) {
      const ImVec2 sample(x + step * 0.5f, y + step * 0.5f);
      const float dist = roundedRectDistance(sample, center, half, radius);
      const float outside = std::max(dist, 0.0f);
      if (outside > extent) continue;

      const float d = outside / std::max(size.y, 1.0f);
      const float px = (sample.x - center.x) / std::max(size.y, 1.0f);
      const float ripple = std::cos(d * 8.5f - px * 1.45f - time * 2.4f - press * 2.0f);
      const float rippleGate = 0.075f / std::exp(std::abs(ripple) / 0.18f);
      const float denom = std::max(d * d + 0.0008f, d + rippleGate);
      const float energy = 0.011f / denom;
      const float compressed = std::sqrt(std::tanh(energy));
      const float fade = 1.0f - smoothstep(extent * 0.25f, extent, outside);
      const float alpha = std::clamp(compressed * fade * gain * 0.38f, 0.0f, 0.58f);
      if (alpha < 0.010f) continue;

      const float phase = px / (std::abs(d) + 0.38f) + time * 1.65f + press * 1.2f;
      const ImVec4 color(0.50f + 0.50f * std::cos(phase + 6.0f),
                         0.42f + 0.48f * std::cos(phase + 1.0f),
                         0.56f + 0.44f * std::cos(phase + 2.0f), alpha);
      draw->AddRectFilled({x, y}, {x + step + 0.6f, y + step + 0.6f}, colorU32(color), 1.5f);
    }
  }
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string normalizeDialogPath(const char* rawPath) {
  if (!rawPath) return {};
  std::string path(rawPath);
  constexpr std::string_view fileScheme = "file://";
  if (path.rfind(fileScheme, 0) != 0) return path;

  path.erase(0, fileScheme.size());
  if (!path.empty() && path[0] != '/') path.insert(path.begin(), '/');

  std::string decoded{};
  decoded.reserve(path.size());
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == '%' && i + 2 < path.size()) {
      const int hi = hexValue(path[i + 1]);
      const int lo = hexValue(path[i + 2]);
      if (hi >= 0 && lo >= 0) {
        decoded.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    decoded.push_back(path[i]);
  }
  return decoded;
}

SidebarPalette sidebarPalette() { return PhotonUi::palette(); }

void drawSidebarHeader(float width, const SidebarPalette& palette, std::string_view activePage) {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float height = 64.0f;
  const float logo = 34.0f;
  const ImVec2 logoMin(pos.x, pos.y + 8.0f);
  const ImVec2 logoMax(logoMin.x + logo, logoMin.y + logo);
  draw->AddRectFilled(logoMin, logoMax, colorU32(withAlpha(palette.active, 0.86f)), 8.0f);
  draw->AddRect(logoMin, logoMax, colorU32(withAlpha(palette.accent, 0.65f)), 8.0f);
  const char* mark = PhotonUi::tabIcon(activePage);
  const ImVec2 markSize = ImGui::CalcTextSize(mark);
  draw->AddText({logoMin.x + (logo - markSize.x) * 0.5f, logoMin.y + (logo - markSize.y) * 0.5f},
                colorU32(palette.text), mark);

  const float textX = logoMax.x + 12.0f;
  draw->PushClipRect({textX, pos.y}, {pos.x + width, pos.y + height - 1.0f}, true);
  draw->AddText({textX, pos.y + 7.0f}, colorU32(palette.text), "Workspace");
  draw->AddText({textX, pos.y + 29.0f}, colorU32(palette.muted), activePage.data(),
                activePage.data() + activePage.size());
  draw->PopClipRect();
  draw->AddLine({pos.x, pos.y + height - 1.0f}, {pos.x + width, pos.y + height - 1.0f},
                colorU32(withAlpha(palette.border, 0.55f)));
  ImGui::Dummy({width, height});
}

bool drawNavItem(const Tab& tab, int index, bool selected, float width,
                 const SidebarPalette& palette) {
  const ImGuiStyle& style = ImGui::GetStyle();
  const float dt = ImGui::GetIO().DeltaTime;
  const float height = 42.0f;
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::PushID(index);
  ImGui::InvisibleButton("nav", {width, height});
  const bool clicked = ImGui::IsItemClicked();
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  const ImGuiID id = ImGui::GetItemID();
  const float target = selected ? 1.0f : hovered ? 0.42f : 0.0f;
  const float focus =
      iam_tween_float(id, ImHashStr("focus"), target, 0.18f, iam_ease_preset(iam_ease_out_quad),
                      iam_policy_crossfade, dt, selected ? 1.0f : 0.0f);
  const float press = iam_tween_float(id, ImHashStr("press"), active ? 1.0f : 0.0f, 0.10f,
                                      iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade, dt);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float inset = 2.0f + press * 1.5f;
  const ImVec2 bgMin(min.x + inset, min.y + 2.0f + press);
  const ImVec2 bgMax(max.x - inset, max.y - 2.0f + press);
  const ImVec4 fill = mixColor(withAlpha(palette.raised, 0.0f), palette.active, focus);
  constexpr float rounding = 8.0f;
  if (focus > 0.01f)
    ImGui::RenderFrame(bgMin, bgMax, colorU32(withAlpha(fill, 0.88f)), false, rounding);
  if (selected)
    PhotonUi::leftAccentFrame(bgMin, bgMax,
                              colorU32(withAlpha(palette.accent, 0.78f + focus * 0.22f)),
                              rounding, 5.0f);

  const float iconBox = 28.0f;
  const ImVec2 iconMin(bgMin.x + 10.0f, bgMin.y + (bgMax.y - bgMin.y - iconBox) * 0.5f);
  const ImVec2 iconMax(iconMin.x + iconBox, iconMin.y + iconBox);

  const char* icon = PhotonUi::tabIcon(tab.name);
  const ImVec2 iconSize = ImGui::CalcTextSize(icon);
  draw->AddText(
      {iconMin.x + (iconBox - iconSize.x) * 0.5f, iconMin.y + (iconBox - iconSize.y) * 0.5f},
      colorU32(selected ? palette.text : mixColor(palette.muted, palette.text, focus)), icon);

  const ImVec4 labelColor = selected ? palette.text : mixColor(palette.muted, palette.text, focus);
  draw->PushClipRect({iconMax.x + 12.0f, bgMin.y}, {bgMax.x - 26.0f, bgMax.y}, true);
  draw->AddText(
      {iconMax.x + 12.0f, bgMin.y + (bgMax.y - bgMin.y - ImGui::GetTextLineHeight()) * 0.5f},
      colorU32(labelColor), tab.name.c_str());
  draw->PopClipRect();

  if (hovered || selected) {
    const ImVec2 chevronSize = ImGui::CalcTextSize("\uE5CC");
    draw->AddText(
        {bgMax.x - chevronSize.x - 10.0f, bgMin.y + (bgMax.y - bgMin.y - chevronSize.y) * 0.5f},
        colorU32(withAlpha(palette.text, selected ? 0.70f : 0.34f)), "\uE5CC");
  }

  PhotonUi::tooltip(tab.name);
  ImGui::PopID();
  ImGui::SetCursorScreenPos({pos.x, pos.y + height + style.ItemSpacing.y * 0.35f});
  return clicked;
}

bool drawActionIcon(const char* id, const char* icon, std::string_view tooltip, ImVec2 size,
                    const SidebarPalette& palette, bool notification = false) {
  const float dt = ImGui::GetIO().DeltaTime;
  ImGui::PushID(id);
  ImGui::InvisibleButton("action", size);
  const bool clicked = ImGui::IsItemClicked();
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  const ImGuiID itemId = ImGui::GetItemID();
  const float target = active ? 1.0f : hovered ? 0.62f : 0.0f;
  const float focus = iam_tween_float(itemId, ImHashStr("focus"), target, 0.14f,
                                      iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade, dt);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  if (notification) drawNotificationField(draw, min, max, focus, active ? 1.0f : 0.0f);
  const ImVec4 fill = mixColor(palette.raised, palette.active, focus);
  draw->AddRectFilled(min, max, colorU32(withAlpha(fill, 0.88f)), 8.0f);
  const ImVec4 border = notification ? mixColor(palette.border, ImVec4(0.58f, 0.40f, 0.95f, 1.0f),
                                                0.22f + focus * 0.28f)
                                     : palette.border;
  draw->AddRect(min, max, colorU32(withAlpha(border, 0.42f + focus * 0.24f)), 8.0f);
  const ImVec2 iconSize = ImGui::CalcTextSize(icon);
  const ImVec4 iconColor =
      notification ? mixColor(palette.text, ImVec4(0.70f, 0.68f, 1.0f, 1.0f), 0.24f + focus * 0.22f)
                   : mixColor(palette.muted, palette.text, 0.35f + focus * 0.65f);
  draw->AddText({min.x + (size.x - iconSize.x) * 0.5f, min.y + (size.y - iconSize.y) * 0.5f},
                colorU32(iconColor), icon);
  PhotonUi::tooltip(tooltip);
  ImGui::PopID();
  return clicked;
}

bool drawDBCButton(std::string_view current, std::string_view status, float width,
                   const SidebarPalette& palette) {
  const float height = 54.0f;
  const float dt = ImGui::GetIO().DeltaTime;
  ImGui::InvisibleButton("dbc_selector", {width, height});
  const bool clicked = ImGui::IsItemClicked();
  const bool hovered = ImGui::IsItemHovered();
  const ImGuiID id = ImGui::GetItemID();
  const float focus = iam_tween_float(id, ImHashStr("focus"), hovered ? 1.0f : 0.0f, 0.16f,
                                      iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade, dt);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(min, max, colorU32(mixColor(palette.panel, palette.raised, focus)), 8.0f);
  draw->AddRect(min, max, colorU32(withAlpha(palette.border, 0.48f + focus * 0.22f)), 8.0f);
  draw->AddCircleFilled({min.x + 18.0f, min.y + 18.0f}, 4.0f, colorU32(palette.accent));
  draw->AddText({min.x + 30.0f, min.y + 9.0f}, colorU32(palette.text), "DBC");
  draw->PushClipRect({min.x + 30.0f, min.y + 28.0f}, {max.x - 34.0f, max.y - 4.0f}, true);
  draw->AddText({min.x + 30.0f, min.y + 30.0f}, colorU32(palette.muted), current.data(),
                current.data() + current.size());
  draw->PopClipRect();
  const char* icon = "\uE313";
  const ImVec2 iconSize = ImGui::CalcTextSize(icon);
  draw->AddText({max.x - iconSize.x - 12.0f, min.y + (height - iconSize.y) * 0.5f},
                colorU32(palette.muted), icon);
  PhotonUi::tooltip(status);
  return clicked;
}

bool drawDBCOption(const char* label, bool selected, float width, const SidebarPalette& palette) {
  const float dt = ImGui::GetIO().DeltaTime;
  const float height = 36.0f;
  ImGui::PushID(label);
  ImGui::InvisibleButton("dbc_option", {width, height});
  const bool clicked = ImGui::IsItemClicked();
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  const ImGuiID id = ImGui::GetItemID();
  const float target = selected ? 1.0f : hovered ? 0.45f : 0.0f;
  const float focus =
      iam_tween_float(id, ImHashStr("focus"), target, 0.15f, iam_ease_preset(iam_ease_out_quad),
                      iam_policy_crossfade, dt, selected ? 1.0f : 0.0f);
  const float press = iam_tween_float(id, ImHashStr("press"), active ? 1.0f : 0.0f, 0.08f,
                                      iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade, dt);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 bgMin(min.x + 1.0f + press, min.y + 2.0f + press);
  const ImVec2 bgMax(max.x - 1.0f - press, max.y - 2.0f + press);
  if (focus > 0.01f)
    draw->AddRectFilled(bgMin, bgMax,
                        colorU32(withAlpha(mixColor(palette.raised, palette.active, focus), 0.88f)),
                        7.0f);
  const char* icon = selected ? "\uE5CA" : "\uE061";
  const ImVec2 iconSize = ImGui::CalcTextSize(icon);
  draw->AddText({bgMin.x + 10.0f, bgMin.y + (bgMax.y - bgMin.y - iconSize.y) * 0.5f},
                colorU32(selected ? palette.accent : mixColor(palette.muted, palette.text, focus)),
                icon);
  draw->PushClipRect({bgMin.x + 38.0f, bgMin.y}, {bgMax.x - 8.0f, bgMax.y}, true);
  draw->AddText(
      {bgMin.x + 38.0f, bgMin.y + (bgMax.y - bgMin.y - ImGui::GetTextLineHeight()) * 0.5f},
      colorU32(selected ? palette.text : mixColor(palette.muted, palette.text, focus)), label);
  draw->PopClipRect();
  PhotonUi::tooltip(label);
  ImGui::PopID();
  return clicked;
}

void drawUploadIcon(ImDrawList* draw, ImVec2 min, float height, ImU32 color) {
  const float x = min.x + 21.0f;
  const float y = min.y + height * 0.5f;
  draw->AddLine({x, y + 5.0f}, {x, y - 6.0f}, color, 2.0f);
  draw->AddLine({x, y - 6.0f}, {x - 4.5f, y - 1.5f}, color, 2.0f);
  draw->AddLine({x, y - 6.0f}, {x + 4.5f, y - 1.5f}, color, 2.0f);
  draw->AddLine({x - 8.0f, y + 7.0f}, {x + 8.0f, y + 7.0f}, color, 2.0f);
}

bool drawPopupAction(const char* id, const char* icon, std::string_view label, bool disabled,
                     float width, const SidebarPalette& palette, bool uploadIcon = false) {
  const float dt = ImGui::GetIO().DeltaTime;
  const float height = 38.0f;
  ImGui::PushID(id);
  if (disabled) ImGui::BeginDisabled();
  ImGui::InvisibleButton("popup_action", {width, height});
  const bool clicked = ImGui::IsItemClicked() && !disabled;
  const bool hovered = ImGui::IsItemHovered() && !disabled;
  const bool active = ImGui::IsItemActive() && !disabled;
  if (disabled) ImGui::EndDisabled();
  const ImGuiID itemId = ImGui::GetItemID();
  const float target = active ? 1.0f : hovered ? 0.62f : 0.0f;
  const float focus = iam_tween_float(itemId, ImHashStr("focus"), target, 0.14f,
                                      iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade, dt);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec4 fill = disabled ? withAlpha(palette.panel, 0.55f)
                               : withAlpha(mixColor(palette.raised, palette.active, focus), 0.88f);
  const ImVec4 text = disabled ? withAlpha(palette.muted, 0.65f)
                               : mixColor(palette.muted, palette.text, 0.48f + focus * 0.52f);
  draw->AddRectFilled(min, max, colorU32(fill), 8.0f);
  draw->AddRect(min, max, colorU32(withAlpha(palette.border, 0.40f + focus * 0.24f)), 8.0f);
  if (uploadIcon) {
    drawUploadIcon(draw, min, height, colorU32(text));
  } else {
    const ImVec2 iconSize = ImGui::CalcTextSize(icon);
    draw->AddText({min.x + 12.0f, min.y + (height - iconSize.y) * 0.5f}, colorU32(text), icon);
  }
  draw->PushClipRect({min.x + 38.0f, min.y}, {max.x - 10.0f, max.y}, true);
  draw->AddText({min.x + 38.0f, min.y + (height - ImGui::GetTextLineHeight()) * 0.5f},
                colorU32(text), label.data(), label.data() + label.size());
  draw->PopClipRect();
  PhotonUi::tooltip(label);
  ImGui::PopID();
  return clicked;
}

void drawDBCError(std::string_view error, float width, const SidebarPalette& palette) {
  if (error.empty()) return;
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float height = std::max(
      34.0f,
      ImGui::CalcTextSize(error.data(), error.data() + error.size(), false, width - 22.0f).y +
          16.0f);
  const ImVec2 max(pos.x + width, pos.y + height);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(pos, max, colorU32(withAlpha(palette.active, 0.34f)), 8.0f);
  draw->AddRect(pos, max, colorU32(withAlpha(palette.accent, 0.32f)), 8.0f);
  ImGui::SetCursorScreenPos({pos.x + 10.0f, pos.y + 8.0f});
  ImGui::PushTextWrapPos(pos.x + width - 10.0f);
  ImGui::TextUnformatted(error.data(), error.data() + error.size());
  ImGui::PopTextWrapPos();
  ImGui::SetCursorScreenPos(pos);
  ImGui::Dummy({width, height});
}

bool drawResizeHandle(float sidebarWidth, float height, const SidebarPalette& palette) {
  const float hitWidth = 18.0f;
  const float railWidth = 3.0f;
  ImGui::SetCursorPos({sidebarWidth - hitWidth * 0.5f, 0.0f});
  ImGui::InvisibleButton("##resizeSidebar", {hitWidth, height});
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  const float dt = ImGui::GetIO().DeltaTime;
  const ImGuiID id = ImGui::GetItemID();
  const float focus =
      iam_tween_float(id, ImHashStr("resizeFocus"),
                      active    ? 1.0f
                      : hovered ? 0.62f
                                : 0.0f,
                      0.14f, iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade, dt);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const float x = min.x + hitWidth * 0.5f;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(
      {x - railWidth * 0.5f, min.y + 10.0f}, {x + railWidth * 0.5f, max.y - 10.0f},
      colorU32(withAlpha(mixColor(palette.border, palette.accent, focus), 0.34f + focus * 0.46f)),
      3.0f);
  if (focus > 0.05f) {
    const float gripH = 24.0f;
    const float gripY = min.y + (height - gripH) * 0.5f;
    const ImU32 gripColor = colorU32(withAlpha(palette.text, 0.22f + focus * 0.42f));
    draw->AddLine({x - 4.0f, gripY}, {x - 4.0f, gripY + gripH}, gripColor, 1.6f);
    draw->AddLine({x + 4.0f, gripY}, {x + 4.0f, gripY + gripH}, gripColor, 1.6f);
  }
  if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  return active;
}

void SDLCALL dbcFileDialogCallback(void* userdata, const char* const* filelist, int filter) {
  (void)filter;
  auto* sidebar = static_cast<Sidebar*>(userdata);
  if (!sidebar) return;

  std::lock_guard lock(sidebar->dbcDialogMutex);
  sidebar->dbcDialogActive = false;
  if (!filelist) {
    sidebar->dbcStatus = std::string("DBC file dialog failed: ") + SDL_GetError();
    sidebar->hasPendingDBCPath = false;
    return;
  }
  if (!filelist[0]) return;

  sidebar->pendingDBCPath = normalizeDialogPath(filelist[0]);
  sidebar->dbcStatus.clear();
  sidebar->hasPendingDBCPath = true;
}
}  // namespace

ImTextureData* loadImguiTexture(const unsigned char* data, std::size_t size) {
  int w = 0, h = 0, comp = 0;
  unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
  if (!pixels) return nullptr;
  ImTextureData* tex = IM_NEW(ImTextureData)();
  tex->Create(ImTextureFormat_RGBA32, w, h);
  memcpy(tex->Pixels, pixels, w * h * 4);
  tex->UseColors = true;

  ImGui::RegisterUserTexture(tex);
  stbi_image_free(pixels);
  return tex;
}

void Sidebar::drawDBCSelector(GUI& gui) {
  std::string selectedPath{};
  {
    std::lock_guard lock(dbcDialogMutex);
    if (hasPendingDBCPath) {
      selectedPath = std::move(pendingDBCPath);
      pendingDBCPath.clear();
      hasPendingDBCPath = false;
    }
  }

  bool closeDBCModal = false;
  if (!selectedPath.empty()) {
    const bool loaded = gui.network && gui.network->switchDBCFile(selectedPath);
    std::lock_guard lock(dbcDialogMutex);
    dbcStatus = loaded ? "" : "Failed to load " + selectedPath;
    closeDBCModal = loaded;
  }

  const SidebarPalette palette = sidebarPalette();
  const float fullWidth = ImGui::GetContentRegionAvail().x;
  Parse* parse = gui.network ? gui.network->parse : nullptr;
  std::string status{};
  bool dialogActive = false;
  {
    std::lock_guard lock(dbcDialogMutex);
    status = dbcStatus;
    dialogActive = dbcDialogActive;
  }
  const char* current = parse ? parse->currentDBCName() : "None";
  if (drawDBCButton(current, status, fullWidth, palette)) ImGui::OpenPopup("Select DBC");

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 modalCenter = {viewport->Pos.x + viewport->Size.x * 0.5f,
                              viewport->Pos.y + viewport->Size.y * 0.5f};
  const float maxModalWidth = std::max(280.0f, viewport->Size.x - 48.0f);
  const float modalWidth = std::min(440.0f, maxModalWidth);
  ImGui::SetNextWindowPos(modalCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2{modalWidth, 0.0f},
                                      ImVec2{modalWidth, viewport->Size.y * 0.82f});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, withAlpha(palette.bg, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, withAlpha(palette.bg, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(palette.border, 0.72f));
  const ImGuiWindowFlags modalFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::BeginPopupModal("Select DBC", nullptr, modalFlags)) {
    const float popupWidth = ImGui::GetContentRegionAvail().x;

    for (uint32_t i = 0; i < Parse::dbcCount(); i++) {
      const DBCType kind = static_cast<DBCType>(i);
      if (kind == DBCType::File) continue;
      const bool selected = parse && parse->activeDBC == kind;
      if (drawDBCOption(Parse::dbcName(kind), selected, popupWidth, palette)) {
        const bool loaded = gui.network && gui.network->switchDBC(kind);
        std::lock_guard lock(dbcDialogMutex);
        dbcStatus = loaded ? "" : std::string("Failed to load ") + Parse::dbcName(kind);
        if (loaded) ImGui::CloseCurrentPopup();
      }
    }

    if (drawPopupAction("UploadFile", "", dialogActive ? "Opening file picker" : "Upload file",
                        dialogActive, popupWidth, palette, true)) {
      {
        std::lock_guard lock(dbcDialogMutex);
        pendingDBCPath.clear();
        hasPendingDBCPath = false;
        dbcStatus.clear();
        dbcDialogActive = true;
      }
      SDL_ShowOpenFileDialog(dbcFileDialogCallback, this, gui.gpu ? gui.gpu->window : nullptr,
                             kDBCFileFilters, static_cast<int>(std::size(kDBCFileFilters)), nullptr,
                             false);
    }

    drawDBCError(status, popupWidth, palette);

    if (drawPopupAction("Close", "\uE5CD", "Close", false, popupWidth, palette))
      ImGui::CloseCurrentPopup();
    if (closeDBCModal) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
  }
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(6);
}

void Sidebar::draw(GUI& gui) {
  auto& titleBar = gui.titleBar;
  auto& tabs = gui.tabs;
  ImVec2 winSize = ImGui::GetMainViewport()->Size;
  const float minSidebarWidth = std::min(240.0f, winSize.x * 0.60f);
  const float maxSidebarWidth = std::max(minSidebarWidth, winSize.x * 0.5f);
  storedWidth = std::clamp(storedWidth, minSidebarWidth, maxSidebarWidth);
  const float targetWidth = titleBar.showSidebar ? storedWidth : 0.0f;
  width = iam_tween_float(ImHashStr("SidebarPanel"), ImHashStr("width"), targetWidth, 0.22f,
                          iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade,
                          ImGui::GetIO().DeltaTime, targetWidth);
  if (width < 2.0f) return;
  float sideBarHeight = winSize.y - (float)titleBar.height;
  ImVec2 pos = {0, titleBar.height};
  ImVec2 dim = {width, sideBarHeight};
  ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNavFocus |
                                 ImGuiWindowFlags_NoDocking;
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(dim);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
  ImVec4 windowBgColor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        {windowBgColor.x, windowBgColor.y, windowBgColor.z, 0.85});
  if (ImGui::Begin("sideBar", NULL, windowFlags)) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const SidebarPalette palette = sidebarPalette();
    const ImVec2 windowMin = ImGui::GetWindowPos();
    const ImVec2 windowMax = {windowMin.x + width, windowMin.y + sideBarHeight};
    draw->AddLine({windowMax.x - 1.0f, windowMin.y}, {windowMax.x - 1.0f, windowMax.y},
                  colorU32(withAlpha(palette.border, 0.55f)));
    ImVec2 padding = ImGui::GetStyle().WindowPadding;
    const float contentWidth = std::max(0.0f, width - padding.x * 2.0f);
    if (width > 180.0f) {
      std::string_view activePage = "Navigation";
      if (!tabs.list.empty() && tabs.index < tabs.list.size())
        activePage = tabs.list[tabs.index].name;
      drawSidebarHeader(contentWidth, palette, activePage);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
      for (int i{0uz}; i < tabs.list.size(); ++i) {
        if (drawNavItem(tabs.list[i], i, tabs.index == i, contentWidth, palette)) tabs.index = i;
      }
      float buttonW = (contentWidth - ImGui::GetStyle().ItemSpacing.x * 3.0f) * 0.25f;
      float buttonH = 38.0f;
      ImVec2 framePadding = ImGui::GetStyle().FramePadding;
      float spacingY = ImGui::GetStyle().ItemSpacing.y;
      float selectorH = 54.0f + spacingY;
      float rowH = selectorH + buttonH + spacingY + framePadding.y * 2.0f;
      pos = {padding.x, sideBarHeight - rowH};
      ImGui::SetCursorPos(pos);
      drawDBCSelector(gui);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + spacingY * 0.25f);
      if (drawActionIcon("Theme", "\uE518", "Theme", {buttonW, buttonH}, palette))
        ImGui::OpenPopup("Theme");
      ImGui::SameLine();
      if (drawActionIcon("Settings", "\uE8B8", "Settings", {buttonW, buttonH}, palette))
        ImGui::OpenPopup("Settings");
      ImGui::SameLine();

      const ImVec2 buttonMin = ImGui::GetCursorScreenPos();
      const ImVec2 buttonMax(buttonMin.x + buttonW, buttonMin.y + buttonH);
      gui.drawButtonShaderOverlay(buttonMin, buttonMax);
      if (drawActionIcon("Update", "\uE8D7", "Update", {buttonW, buttonH}, palette, false))
        ImGui::OpenPopup("Update");
      ImGui::SameLine();
      if (drawActionIcon("Export", "\uE89E", "Export", {buttonW, buttonH}, palette))
        ImGui::OpenPopup("Export");
      gui.settings.colorUI();
      gui.settingsUI();
      gui.updateUI();
      gui.exportUI();
    }

    if (titleBar.showSidebar && drawResizeHandle(width, dim.y, palette)) {
      storedWidth =
          std::clamp(storedWidth + ImGui::GetIO().MouseDelta.x, minSidebarWidth, maxSidebarWidth);
      width = storedWidth;
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(1);
};
