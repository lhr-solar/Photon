#include "sideBar.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>

#include <algorithm>
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
  PhotonUi::drawIconCentered(draw, mark, logoMin, logoMax, 19.0f, colorU32(palette.text), 1.0f);

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
  const float height = 42.0f;
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::PushID(index);
  const PhotonUi::ControlState state =
      PhotonUi::control("nav", {width, height}, selected, 0.42f, 0.88f, 0.18f, 0.10f);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float inset = 2.0f + state.press * 1.5f;
  const ImVec2 bgMin(state.min.x + inset, state.min.y + 2.0f + state.press);
  const ImVec2 bgMax(state.max.x - inset, state.max.y - 2.0f + state.press);
  const ImVec4 fill = mixColor(withAlpha(palette.raised, 0.0f), palette.active, state.focus);
  constexpr float rounding = 8.0f;
  if (state.focus > 0.01f)
    ImGui::RenderFrame(bgMin, bgMax, colorU32(withAlpha(fill, 0.88f)), false, rounding);

  const float iconBox = 28.0f;
  const ImVec2 iconMin(bgMin.x + 10.0f, bgMin.y + (bgMax.y - bgMin.y - iconBox) * 0.5f);
  const ImVec2 iconMax(iconMin.x + iconBox, iconMin.y + iconBox);

  const char* icon = PhotonUi::tabIcon(tab.name);
  PhotonUi::drawIconCentered(
      draw, icon, iconMin, iconMax, 17.0f,
      colorU32(selected ? palette.text : mixColor(palette.muted, palette.text, state.focus)), 1.0f);

  const ImVec4 labelColor =
      selected ? palette.text : mixColor(palette.muted, palette.text, state.focus);
  draw->PushClipRect({iconMax.x + 12.0f, bgMin.y}, {bgMax.x - 26.0f, bgMax.y}, true);
  draw->AddText(
      {iconMax.x + 12.0f, bgMin.y + (bgMax.y - bgMin.y - ImGui::GetTextLineHeight()) * 0.5f},
      colorU32(labelColor), tab.name.c_str());
  draw->PopClipRect();

  if (state.hovered || selected) {
    const ImVec2 chevronMin(bgMax.x - 26.0f, bgMin.y);
    PhotonUi::drawIconCentered(draw, "\uea61", chevronMin, {bgMax.x - 8.0f, bgMax.y}, 15.0f,
                               colorU32(withAlpha(palette.text, selected ? 0.70f : 0.34f)), 1.0f);
  }

  PhotonUi::tooltip(tab.name);
  ImGui::PopID();
  ImGui::SetCursorScreenPos({pos.x, pos.y + height + style.ItemSpacing.y * 0.35f});
  return state.clicked;
}

bool drawActionIcon(const char* id, const char* icon, std::string_view tooltip, ImVec2 size,
                    const SidebarPalette& palette) {
  return PhotonUi::iconButton(id, icon, tooltip, size, palette);
}

bool drawDBCButton(std::string_view current, std::string_view status, float width,
                   const SidebarPalette& palette) {
  const float height = 54.0f;
  const PhotonUi::ControlState state =
      PhotonUi::control("dbc_selector", {width, height}, false, 1.0f, 1.0f, 0.16f);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(state.min, state.max,
                      colorU32(mixColor(palette.panel, palette.raised, state.focus)), 8.0f);
  draw->AddRect(state.min, state.max,
                colorU32(withAlpha(palette.border, 0.48f + state.focus * 0.22f)), 8.0f);
  draw->AddCircleFilled({state.min.x + 18.0f, state.min.y + 18.0f}, 4.0f, colorU32(palette.accent));
  draw->AddText({state.min.x + 30.0f, state.min.y + 9.0f}, colorU32(palette.text), "DBC");
  draw->PushClipRect({state.min.x + 30.0f, state.min.y + 28.0f},
                     {state.max.x - 34.0f, state.max.y - 4.0f}, true);
  draw->AddText({state.min.x + 30.0f, state.min.y + 30.0f}, colorU32(palette.muted), current.data(),
                current.data() + current.size());
  draw->PopClipRect();
  PhotonUi::drawIconCentered(draw, "\uea5f", {state.max.x - 30.0f, state.min.y},
                             {state.max.x - 10.0f, state.max.y}, 15.0f, colorU32(palette.muted),
                             1.0f);
  PhotonUi::tooltip(status);
  return state.clicked;
}

bool drawDBCOption(const char* label, bool selected, float width, const SidebarPalette& palette) {
  const char* icon = selected ? "\uea5e" : "\uea6b";
  return PhotonUi::rowButton(label, icon, label, {width, 36.0f}, palette, selected);
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
  if (!uploadIcon)
    return PhotonUi::rowButton(id, icon, label, {width, 38.0f}, palette, false, disabled);

  const float height = 38.0f;
  ImGui::PushID(id);
  if (disabled) ImGui::BeginDisabled();
  const PhotonUi::ControlState state =
      PhotonUi::control("popup_action", {width, height}, false, 0.62f, 1.0f);
  if (disabled) ImGui::EndDisabled();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec4 fill = disabled
                          ? withAlpha(palette.panel, 0.55f)
                          : withAlpha(mixColor(palette.raised, palette.active, state.focus), 0.88f);
  const ImVec4 text = disabled ? withAlpha(palette.muted, 0.65f)
                               : mixColor(palette.muted, palette.text, 0.48f + state.focus * 0.52f);
  draw->AddRectFilled(state.min, state.max, colorU32(fill), 8.0f);
  draw->AddRect(state.min, state.max,
                colorU32(withAlpha(palette.border, 0.40f + state.focus * 0.24f)), 8.0f);
  drawUploadIcon(draw, state.min, height, colorU32(text));
  draw->PushClipRect({state.min.x + 38.0f, state.min.y}, {state.max.x - 10.0f, state.max.y}, true);
  draw->AddText({state.min.x + 38.0f, state.min.y + (height - ImGui::GetTextLineHeight()) * 0.5f},
                colorU32(text), label.data(), label.data() + label.size());
  draw->PopClipRect();
  PhotonUi::tooltip(label);
  ImGui::PopID();
  return state.clicked && !disabled;
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

  const bool open = PhotonUi::beginModal("Select DBC", {440.0f, 360.0f});
  if (open) {
    PhotonUi::label("Select DBC", palette);
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

    if (PhotonUi::modalCloseButton("CloseDBCSelector", palette)) ImGui::CloseCurrentPopup();
    if (closeDBCModal) ImGui::CloseCurrentPopup();
  }
  PhotonUi::endModal(open);
}

void Sidebar::draw(GUI& gui) {
  auto& titleBar = gui.titleBar;
  auto& tabs = gui.tabs;
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 viewportPos = viewport->Pos;
  const ImVec2 viewportSize = viewport->Size;
  ImVec2 winSize = viewportSize;
  const float minSidebarWidth = std::min(240.0f, winSize.x * 0.60f);
  const float maxSidebarWidth = std::max(minSidebarWidth, winSize.x * 0.5f);
  storedWidth = std::clamp(storedWidth, minSidebarWidth, maxSidebarWidth);
  const float targetWidth = titleBar.showSidebar ? storedWidth : 0.0f;
  width = iam_tween_float(ImHashStr("SidebarPanel"), ImHashStr("width"), targetWidth, 0.22f,
                          iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade,
                          ImGui::GetIO().DeltaTime, targetWidth);
  if (width < 2.0f) return;
  float sideBarHeight = winSize.y - (float)titleBar.height;
  ImVec2 pos = {viewportPos.x, viewportPos.y + titleBar.height + 2.0f};
  ImVec2 dim = {width, sideBarHeight};
  ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNavFocus |
                                 ImGuiWindowFlags_NoDocking;
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(dim);
  ImGui::SetNextWindowViewport(viewport->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImVec4 windowBgColor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        {windowBgColor.x, windowBgColor.y, windowBgColor.z, 0.85});
  if (ImGui::Begin("sideBar", NULL, windowFlags)) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const SidebarPalette palette = sidebarPalette();
    const ImVec2 windowMin = ImGui::GetWindowPos();
    const ImVec2 windowMax = {windowMin.x + width, windowMin.y + sideBarHeight};
    const float timelineTop = viewportPos.y + viewportSize.y - titleBar.height;
    draw->AddLine({windowMax.x - 1.0f, windowMin.y}, {windowMax.x - 1.0f, timelineTop},
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
      if (drawActionIcon("Theme", "\ueb01", "Theme", {buttonW, buttonH}, palette))
        ImGui::OpenPopup("Theme");
      ImGui::SameLine();
      if (drawActionIcon("Settings", "\ueb20", "Settings", {buttonW, buttonH}, palette))
        ImGui::OpenPopup("Settings");
      ImGui::SameLine();

      const ImVec2 buttonMin = ImGui::GetCursorScreenPos();
      const ImVec2 buttonMax(buttonMin.x + buttonW, buttonMin.y + buttonH);
      gui.drawButtonShaderOverlay(buttonMin, buttonMax);
      if (drawActionIcon("Update", "\ueb13", "Update", {buttonW, buttonH}, palette))
        ImGui::OpenPopup("Update");
      ImGui::SameLine();
      if (drawActionIcon("Export", "\uede9", "Export", {buttonW, buttonH}, palette))
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
