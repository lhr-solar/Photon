#include "customView.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>

#include <algorithm>
#include <cmath>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>

#include "json.hpp"
#include "uiComponents.hpp"

namespace {
using Json = nlohmann::json;

constexpr SDL_DialogFileFilter kViewFileFilters[] = {
    {"Photon view configs", "json"},
    {"All files", "*"},
};

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

bool statusLooksLikeError(std::string_view text) {
  return text.find("failed") != std::string_view::npos ||
         text.find("Failed") != std::string_view::npos ||
         text.find("unresolved") != std::string_view::npos;
}

void SDLCALL viewFileDialogCallback(void* userdata, const char* const* filelist, int filter) {
  (void)filter;
  auto* tab = static_cast<CustomViewTab*>(userdata);
  if (!tab) return;
  std::lock_guard lock(tab->dialogMutex);
  tab->dialogActive = false;
  if (!filelist) {
    tab->pendingDialogError = std::string("File dialog failed: ") + SDL_GetError();
    tab->activeDialogAction = CustomViewDialogAction::None;
    return;
  }
  if (!filelist[0]) {
    tab->activeDialogAction = CustomViewDialogAction::None;
    return;
  }
  tab->pendingDialogPath = normalizeDialogPath(filelist[0]);
  tab->pendingDialogAction = tab->activeDialogAction;
  tab->activeDialogAction = CustomViewDialogAction::None;
}

uint32_t parseMessageId(const Json& value) {
  if (value.is_number_unsigned() || value.is_number_integer()) {
    const int64_t id = value.get<int64_t>();
    if (id < 0 || id >= MESSAGE_MAX) throw std::runtime_error("messageId is out of range");
    return static_cast<uint32_t>(id);
  }
  if (!value.is_string()) throw std::runtime_error("messageId must be a number or string");
  const std::string text = value.get<std::string>();
  size_t consumed = 0;
  const unsigned long parsed = std::stoul(text, &consumed, 0);
  if (consumed != text.size() || parsed >= MESSAGE_MAX)
    throw std::runtime_error("invalid messageId: " + text);
  return static_cast<uint32_t>(parsed);
}

std::string readTextFile(const std::filesystem::path& filePath) {
  std::ifstream input(filePath, std::ios::binary);
  if (!input) throw std::runtime_error("could not open " + filePath.string());
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}

void copyDocument(std::array<char, 65536>& target, const std::string& text) {
  target.fill('\0');
  const size_t count = std::min(text.size(), target.size() - 1);
  std::memcpy(target.data(), text.data(), count);
}

Json sourceToJson(const PlotManager::PlotSourceRef& source) {
  Json value = {{"messageId", source.messageId}, {"signalIndex", source.signalIndex}};
  if (!source.messageName.empty()) value["messageName"] = source.messageName;
  if (!source.signalName.empty()) value["signalName"] = source.signalName;
  return value;
}

Json plotToJson(const PlotManager::PlotWindow& plot) {
  Json sources = Json::array();
  for (const auto& source : plot.sources) sources.push_back(sourceToJson(source));
  return {{"type", PlotManager::typeKey(plot.typeIndex)},
          {"title", plot.title},
          {"useSource1TimeAsX", plot.useSource1TimeAsX},
          {"sources", std::move(sources)}};
}

Json widgetToJson(const CustomViewWidget& widget) {
  return {{"id", widget.id},
          {"kind", "plot"},
          {"rect",
           {{"x", widget.rect.x},
            {"y", widget.rect.y},
            {"w", widget.rect.width},
            {"h", widget.rect.height}}},
          {"plot", plotToJson(widget.plot)}};
}

constexpr float kTitleBarHeight = 28.0f;
constexpr float kResizeGrip = 20.0f;
constexpr int kExtraCanvasRows = 6;
}  // namespace

void CustomViewTab::init(Arena* arenaTarget, SDL_Window* windowTarget) {
  if (arena == arenaTarget && window == windowTarget) return;
  arena = arenaTarget;
  window = windowTarget;
  resolvedGeneration = arena ? arena->generation : 0;
  document.fill('\0');
  view = {};
  dirty = false;
  pathLoaded = false;
  absorbArmed = false;
  absorbBaseline = 0;
  interactMode = CustomViewInteractMode::None;
  interactWidgetId.clear();
  if (std::filesystem::exists(path.data()))
    load();
  else
    status = "No custom view loaded. Add a plot or press Ctrl+O to open a view.";
}

void CustomViewTab::showFileDialog(CustomViewDialogAction action) {
  std::lock_guard lock(dialogMutex);
  if (dialogActive || requestedDialogAction != CustomViewDialogAction::None) return;
  requestedDialogAction = action;
  pendingDialogAction = CustomViewDialogAction::None;
  pendingDialogPath.clear();
  pendingDialogError.clear();
}

void CustomViewTab::pumpFileDialogRequest() {
  CustomViewDialogAction action = CustomViewDialogAction::None;
  {
    std::lock_guard lock(dialogMutex);
    if (requestedDialogAction == CustomViewDialogAction::None || dialogActive) return;
    // Opening the native dialog while ImGui still considers the button held leaves the
    // button stuck on ButtonActive (reads as red) and can make Explorer flash+dismiss.
    const ImGuiIO& io = ImGui::GetIO();
    if (io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2]) return;
    action = requestedDialogAction;
    requestedDialogAction = CustomViewDialogAction::None;
    dialogActive = true;
    activeDialogAction = action;
  }

  if (!window) {
    std::lock_guard lock(dialogMutex);
    dialogActive = false;
    activeDialogAction = CustomViewDialogAction::None;
    pendingDialogError = "File dialog failed: Photon window is not ready";
    return;
  }

  if (action == CustomViewDialogAction::Open) {
    SDL_ShowOpenFileDialog(viewFileDialogCallback, this, window, kViewFileFilters,
                           static_cast<int>(std::size(kViewFileFilters)), nullptr, false);
  } else {
    SDL_ShowSaveFileDialog(viewFileDialogCallback, this, window, kViewFileFilters,
                           static_cast<int>(std::size(kViewFileFilters)), nullptr);
  }
}

void CustomViewTab::consumeFileDialog() {
  std::string selectedPath{};
  std::string dialogError{};
  CustomViewDialogAction action = CustomViewDialogAction::None;
  {
    std::lock_guard lock(dialogMutex);
    selectedPath = std::move(pendingDialogPath);
    dialogError = std::move(pendingDialogError);
    pendingDialogPath.clear();
    pendingDialogError.clear();
    action = pendingDialogAction;
    pendingDialogAction = CustomViewDialogAction::None;
  }
  if (!dialogError.empty()) {
    status = std::move(dialogError);
    return;
  }
  if (selectedPath.empty() || action == CustomViewDialogAction::None) return;
  if (action == CustomViewDialogAction::SaveAs && !selectedPath.ends_with(".photon-view.json"))
    selectedPath += ".photon-view.json";
  path.fill('\0');
  const size_t count = std::min(selectedPath.size(), path.size() - 1);
  std::memcpy(path.data(), selectedPath.data(), count);
  if (action == CustomViewDialogAction::Open)
    load();
  else
    exportCurrentView(selectedPath);
}

void CustomViewTab::newDocument() {
  Json root = {{"$schema", "../docs/config/photon-view.schema.json"},
               {"schemaVersion", 1},
               {"id", "custom-view"},
               {"name", "Custom View"},
               {"layout", {{"columns", 12}, {"rowHeight", 160}, {"gap", 12}}},
               {"widgets", Json::array()}};
  copyDocument(document, root.dump(2));
  dirty = true;
  parseDocument();
  status = "New empty view created. Add a plot or import a view.";
}

bool CustomViewTab::load() {
  try {
    const std::filesystem::path filePath(path.data());
    const std::string text = readTextFile(filePath);
    if (text.size() >= document.size())
      throw std::runtime_error("config exceeds 64 KiB editor limit");
    copyDocument(document, text);
    if (!parseDocument()) return false;
    std::error_code error;
    loadedAt = std::filesystem::last_write_time(filePath, error);
    dirty = false;
    pathLoaded = true;
    interactMode = CustomViewInteractMode::None;
    interactWidgetId.clear();
    status = "Loaded and validated " + filePath.string();
    return true;
  } catch (const std::exception& error) {
    status = std::string("Open failed: ") + error.what();
    pathLoaded = false;
    return false;
  }
}

bool CustomViewTab::save() {
  syncDocumentFromView("Saved valid view config to " + std::string(path.data()), true);
  return !statusLooksLikeError(status);
}

bool CustomViewTab::parseDocument() {
  try {
    const Json root = Json::parse(document.data());
    if (root.value("schemaVersion", 0) != 1)
      throw std::runtime_error("unsupported schemaVersion; expected 1");
    CustomViewDefinition next{};
    next.schemaVersion = 1;
    next.id = root.value("id", "custom-view");
    next.name = root.value("name", "Custom View");
    const Json layout = root.value("layout", Json::object());
    next.columns = std::clamp(layout.value("columns", 12), 1, 48);
    next.rowHeight = std::clamp(layout.value("rowHeight", 160.0f), 48.0f, 1200.0f);
    next.gap = std::clamp(layout.value("gap", 12.0f), 0.0f, 64.0f);
    std::unordered_set<std::string> ids;
    int plotId = 10000;
    for (const Json& widgetJson : root.value("widgets", Json::array())) {
      if (widgetJson.value("kind", "") != "plot") continue;
      CustomViewWidget widget{};
      widget.id = widgetJson.value("id", "plot-" + std::to_string(plotId));
      if (!ids.insert(widget.id).second)
        throw std::runtime_error("duplicate widget id: " + widget.id);
      const Json rect = widgetJson.value("rect", Json::object());
      widget.rect.x = std::max(0, rect.value("x", 0));
      widget.rect.y = std::max(0, rect.value("y", 0));
      widget.rect.width = std::clamp(rect.value("w", next.columns), 1, next.columns);
      widget.rect.height = std::clamp(rect.value("h", 2), 1, 24);
      if (widget.rect.x + widget.rect.width > next.columns)
        throw std::runtime_error("widget exceeds layout columns: " + widget.id);
      const Json plotJson = widgetJson.at("plot");
      const std::string type = plotJson.value("type", "line");
      widget.plot.typeIndex = PlotManager::typeFromKey(type);
      if (widget.plot.typeIndex < 0) throw std::runtime_error("unknown plot type: " + type);
      widget.plot.id = plotId++;
      widget.plot.title = plotJson.value("title", widget.id);
      widget.plot.useSource1TimeAsX = plotJson.value("useSource1TimeAsX", true);
      for (const Json& sourceJson : plotJson.value("sources", Json::array())) {
        PlotManager::PlotSourceRef source{};
        source.messageId = parseMessageId(sourceJson.at("messageId"));
        source.messageName = sourceJson.value("messageName", "");
        source.signalName = sourceJson.value("signalName", "");
        source.signalIndex = sourceJson.value("signalIndex", SIGNAL_MAX);
        source.assigned = true;
        widget.plot.sources.push_back(std::move(source));
      }
      const auto& spec = PlotManager::typeSpec(widget.plot.typeIndex);
      const int requiredMin = spec.is3D ? (widget.plot.useSource1TimeAsX ? 2 : 3) : spec.minSources;
      const int requiredMax = spec.is3D ? requiredMin : spec.maxSources;
      const int count = static_cast<int>(widget.plot.sources.size());
      if (count < requiredMin || count > requiredMax)
        throw std::runtime_error(widget.id + " requires " + std::to_string(requiredMin) + " to " +
                                 std::to_string(requiredMax) + " sources");
      next.widgets.push_back(std::move(widget));
    }
    if (next.widgets.size() > 128) throw std::runtime_error("view exceeds 128 widget limit");
    view = std::move(next);
    nextWidgetPlotId = plotId;
    resolveSources();
    dirty = true;
    status = "Config is valid: " + std::to_string(view.widgets.size()) + " plot widget(s).";
    return true;
  } catch (const std::exception& error) {
    status = std::string("Validation failed: ") + error.what();
    return false;
  }
}

void CustomViewTab::resolveSources() {
  if (!arena) return;
  int unresolved = 0;
  for (auto& widget : view.widgets) {
    for (auto& source : widget.plot.sources) {
      source.assigned = false;
      Message* message =
          source.messageId < arena->messages.size() ? arena->messages[source.messageId] : nullptr;
      if (!message) {
        ++unresolved;
        continue;
      }
      uint32_t resolved = SIGNAL_MAX;
      if (!source.signalName.empty()) {
        for (uint32_t index = 0; index < message->signalCount; ++index) {
          if (message->signals[index] && message->signals[index]->name == source.signalName) {
            resolved = index;
            break;
          }
        }
      }
      if (resolved == SIGNAL_MAX && source.signalName.empty() &&
          source.signalIndex < message->signalCount && message->signals[source.signalIndex])
        resolved = source.signalIndex;
      if (resolved == SIGNAL_MAX) {
        ++unresolved;
        continue;
      }
      source.signalIndex = resolved;
      source.messageName = message->name;
      source.signalName = message->signals[resolved]->name;
      source.assigned = true;
    }
  }
  resolvedGeneration = arena->generation;
  if (unresolved > 0)
    status = "Config is structurally valid; " + std::to_string(unresolved) +
             " source(s) are unresolved for the active DBC.";
}

int CustomViewTab::findNextRow() const {
  int row = 0;
  for (const auto& widget : view.widgets) row = std::max(row, widget.rect.y + widget.rect.height);
  return row;
}

CustomViewRect CustomViewTab::clampRect(CustomViewRect rect) const {
  rect.width = std::clamp(rect.width, 1, view.columns);
  rect.height = std::clamp(rect.height, 1, 24);
  rect.x = std::clamp(rect.x, 0, std::max(0, view.columns - rect.width));
  rect.y = std::max(0, rect.y);
  if (rect.x + rect.width > view.columns) rect.width = view.columns - rect.x;
  return rect;
}

CustomViewWidget* CustomViewTab::findWidget(const std::string& id) {
  for (auto& widget : view.widgets)
    if (widget.id == id) return &widget;
  return nullptr;
}

std::string CustomViewTab::buildDocumentJson() const {
  Json widgets = Json::array();
  for (const auto& widget : view.widgets) widgets.push_back(widgetToJson(widget));
  Json root = {
      {"$schema", "../docs/config/photon-view.schema.json"},
      {"schemaVersion", 1},
      {"id", view.id.empty() ? "custom-view" : view.id},
      {"name", view.name.empty() ? "Custom View" : view.name},
      {"layout", {{"columns", view.columns}, {"rowHeight", view.rowHeight}, {"gap", view.gap}}},
      {"widgets", std::move(widgets)}};
  return root.dump(2);
}

void CustomViewTab::syncDocumentFromView(std::string_view statusMessage, bool writeToDisk) {
  try {
    const std::string serialized = buildDocumentJson();
    if (serialized.size() >= document.size())
      throw std::runtime_error("config exceeds 64 KiB editor limit");
    copyDocument(document, serialized);
    dirty = true;
    if (writeToDisk && path[0] != '\0') {
      const std::filesystem::path filePath(path.data());
      if (filePath.has_parent_path()) std::filesystem::create_directories(filePath.parent_path());
      std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
      if (!output) throw std::runtime_error("could not write " + filePath.string());
      output << serialized;
      output.close();
      std::error_code error;
      loadedAt = std::filesystem::last_write_time(filePath, error);
      dirty = false;
      pathLoaded = true;
    }
    status = std::string(statusMessage);
  } catch (const std::exception& error) {
    status = std::string("Layout sync failed: ") + error.what();
  }
}

void CustomViewTab::commitLayoutEdit() {
  syncDocumentFromView("Layout edited.", pathLoaded || path[0] != '\0');
}

void CustomViewTab::absorbCreatedPlots() {
  if (!absorbArmed) return;
  auto& manager = plotManager();
  if (manager.windows.size() <= absorbBaseline) {
    if (!manager.creatorOpen) absorbArmed = false;
    return;
  }

  std::vector<PlotManager::PlotWindow> taken;
  taken.reserve(manager.windows.size() - absorbBaseline);
  for (size_t i = absorbBaseline; i < manager.windows.size(); ++i)
    taken.push_back(std::move(manager.windows[i]));
  manager.windows.resize(absorbBaseline);
  absorbArmed = false;

  int row = findNextRow();
  for (auto& plot : taken) {
    CustomViewWidget widget{};
    widget.id = "plot-" + std::to_string(nextWidgetPlotId++);
    widget.rect = clampRect({0, row, view.columns, 2});
    widget.plot = std::move(plot);
    widget.plot.open = true;
    row = widget.rect.y + widget.rect.height;
    view.widgets.push_back(std::move(widget));
  }
  if (view.id.empty()) view.id = "custom-view";
  if (view.name.empty()) view.name = "Custom View";
  syncDocumentFromView("Added plot(s) to the canvas.", pathLoaded || path[0] != '\0');
}

void CustomViewTab::renderPreview() {
  if (view.widgets.empty()) {
    ImGui::TextDisabled("No plot widgets in this view. Add a plot to place one on the canvas.");
    return;
  }

  const PhotonUi::Palette palette = PhotonUi::palette();
  const float viewportHeight = std::max(160.0f, ImGui::GetContentRegionAvail().y);
  const ImGuiWindowFlags canvasFlags =
      (interactMode != CustomViewInteractMode::None) ? ImGuiWindowFlags_NoScrollWithMouse : 0;

  std::string pendingDelete{};
  if (ImGui::BeginChild("##custom_view_canvas", ImVec2(-1.0f, viewportHeight),
                        ImGuiChildFlags_Borders, canvasFlags)) {
    // Size columns from the scrollable content width so the scrollbar gutter
    // does not sit on top of title bars / the SE resize grip.
    const float available = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    const float cellWidth =
        std::max(8.0f, (available - view.gap * static_cast<float>(view.columns - 1)) /
                           static_cast<float>(view.columns));
    const float colStride = cellWidth + view.gap;
    const float rowStride = view.rowHeight + view.gap;

    int rows = 1;
    for (const auto& widget : view.widgets) {
      const CustomViewRect& rect =
          (interactMode != CustomViewInteractMode::None && interactWidgetId == widget.id)
              ? interactPreviewRect
              : widget.rect;
      rows = std::max(rows, rect.y + rect.height);
    }
    rows += kExtraCanvasRows;
    const float canvasHeight = rows * view.rowHeight + std::max(0, rows - 1) * view.gap;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    for (auto& widget : view.widgets) {
      CustomViewRect displayRect = widget.rect;
      if (interactMode != CustomViewInteractMode::None && interactWidgetId == widget.id)
        displayRect = interactPreviewRect;

      const ImVec2 position(origin.x + displayRect.x * colStride,
                            origin.y + displayRect.y * rowStride);
      const ImVec2 size(displayRect.width * cellWidth + (displayRect.width - 1) * view.gap,
                        displayRect.height * view.rowHeight + (displayRect.height - 1) * view.gap);

      ImGui::SetCursorScreenPos(position);
      ImGui::PushID(widget.id.c_str());
      if (ImGui::BeginChild("##widget", size, ImGuiChildFlags_Borders,
                            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImDrawList* widgetDraw = ImGui::GetWindowDrawList();
        const float closeWidth = 28.0f;
        const float titleWidth = std::max(8.0f, ImGui::GetContentRegionAvail().x - closeWidth);
        ImGui::InvisibleButton("##drag", ImVec2(titleWidth, kTitleBarHeight));
        const bool titleHovered = ImGui::IsItemHovered();
        const bool titleActive = ImGui::IsItemActive();
        const ImVec2 titleMin = ImGui::GetItemRectMin();
        const ImVec2 titleMax = ImGui::GetItemRectMax();
        const ImVec2 barMax(titleMax.x + closeWidth, titleMax.y);
        widgetDraw->AddRectFilled(titleMin, barMax,
                                  PhotonUi::colorU32(PhotonUi::withAlpha(palette.raised, 0.95f)),
                                  4.0f);
        widgetDraw->AddText({titleMin.x + 8.0f,
                             titleMin.y + (kTitleBarHeight - ImGui::GetTextLineHeight()) * 0.5f},
                            PhotonUi::colorU32(palette.muted), "::");
        widgetDraw->AddText({titleMin.x + 28.0f,
                             titleMin.y + (kTitleBarHeight - ImGui::GetTextLineHeight()) * 0.5f},
                            PhotonUi::colorU32(palette.text), widget.plot.title.c_str());
        if (titleHovered || titleActive) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

        ImGui::SameLine(0.0f, 0.0f);
        if (ImGui::SmallButton("X##close")) pendingDelete = widget.id;

        if (titleActive && interactMode == CustomViewInteractMode::None &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
          interactMode = CustomViewInteractMode::Drag;
          interactWidgetId = widget.id;
          interactStartRect = widget.rect;
          interactStartMouse = ImGui::GetIO().MouseClickedPos[0];
          interactPreviewRect = widget.rect;
        }

        ImGui::Separator();
        // Keep a dedicated bottom strip so the grip is not covered by the plot child.
        const float bodyHeight =
            std::max(40.0f, ImGui::GetContentRegionAvail().y - kResizeGrip - 2.0f);
        if (ImGui::BeginChild("##plot_body", ImVec2(-1.0f, bodyHeight), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollbar))
          plotManager().renderEmbedded(widget.plot);
        ImGui::EndChild();

        const float gripX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - kResizeGrip;
        const float gripY = ImGui::GetCursorPosY();
        ImGui::SetCursorPos(ImVec2(gripX, gripY));
        ImGui::InvisibleButton("##resize", ImVec2(kResizeGrip, kResizeGrip));
        const bool resizeHovered = ImGui::IsItemHovered();
        const bool resizeActive = ImGui::IsItemActive();
        const ImVec2 gripMin = ImGui::GetItemRectMin();
        const ImVec2 gripMax = ImGui::GetItemRectMax();
        widgetDraw->AddRectFilled(gripMin, gripMax,
                                  PhotonUi::colorU32(PhotonUi::withAlpha(palette.raised, 0.9f)),
                                  3.0f);
        widgetDraw->AddTriangleFilled(
            {gripMin.x + 3.0f, gripMax.y - 3.0f}, {gripMax.x - 3.0f, gripMax.y - 3.0f},
            {gripMax.x - 3.0f, gripMin.y + 3.0f},
            PhotonUi::colorU32(PhotonUi::withAlpha(palette.accent, resizeHovered ? 1.0f : 0.8f)));
        if (resizeHovered || resizeActive) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);

        if (resizeActive && interactMode == CustomViewInteractMode::None) {
          interactMode = CustomViewInteractMode::Resize;
          interactWidgetId = widget.id;
          interactStartRect = widget.rect;
          interactStartMouse = ImGui::GetIO().MouseClickedPos[0];
          interactPreviewRect = widget.rect;
        }
      }
      ImGui::EndChild();

      if (interactMode != CustomViewInteractMode::None && interactWidgetId == widget.id) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float dx = mouse.x - interactStartMouse.x;
        const float dy = mouse.y - interactStartMouse.y;
        CustomViewRect next = interactStartRect;
        if (interactMode == CustomViewInteractMode::Drag) {
          next.x = interactStartRect.x + static_cast<int>(std::lround(dx / colStride));
          next.y = interactStartRect.y + static_cast<int>(std::lround(dy / rowStride));
        } else {
          next.width =
              interactStartRect.width + static_cast<int>(std::lround(dx / colStride));
          next.height =
              interactStartRect.height + static_cast<int>(std::lround(dy / rowStride));
        }
        interactPreviewRect = clampRect(next);

        const ImVec2 ghostPos(origin.x + interactPreviewRect.x * colStride,
                              origin.y + interactPreviewRect.y * rowStride);
        const ImVec2 ghostSize(
            interactPreviewRect.width * cellWidth + (interactPreviewRect.width - 1) * view.gap,
            interactPreviewRect.height * view.rowHeight +
                (interactPreviewRect.height - 1) * view.gap);
        draw->AddRect(ghostPos, {ghostPos.x + ghostSize.x, ghostPos.y + ghostSize.y},
                      PhotonUi::colorU32(PhotonUi::withAlpha(palette.accent, 0.85f)), 6.0f, 0,
                      2.0f);

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
          widget.rect = interactPreviewRect;
          interactMode = CustomViewInteractMode::None;
          interactWidgetId.clear();
          commitLayoutEdit();
        }
      }

      ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + canvasHeight));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
  }
  ImGui::EndChild();

  if (!pendingDelete.empty()) {
    view.widgets.erase(std::remove_if(view.widgets.begin(), view.widgets.end(),
                                      [&](const CustomViewWidget& widget) {
                                        return widget.id == pendingDelete;
                                      }),
                       view.widgets.end());
    if (interactWidgetId == pendingDelete) {
      interactMode = CustomViewInteractMode::None;
      interactWidgetId.clear();
    }
    syncDocumentFromView("Removed plot from the canvas.", pathLoaded || path[0] != '\0');
  }
}

void CustomViewTab::renderStatus() {
  if (status.empty()) return;
  const PhotonUi::Palette palette = PhotonUi::palette();
  const bool isError = statusLooksLikeError(status);
  const float width = ImGui::GetContentRegionAvail().x;
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float height = std::max(
      34.0f, ImGui::CalcTextSize(status.c_str(), nullptr, false, width - 22.0f).y + 16.0f);
  const ImVec2 max(pos.x + width, pos.y + height);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec4 fill =
      isError ? PhotonUi::withAlpha(palette.active, 0.42f) : PhotonUi::withAlpha(palette.panel, 0.76f);
  const ImVec4 border =
      isError ? PhotonUi::withAlpha(palette.accent, 0.55f) : PhotonUi::withAlpha(palette.border, 0.48f);
  draw->AddRectFilled(pos, max, PhotonUi::colorU32(fill), 8.0f);
  draw->AddRect(pos, max, PhotonUi::colorU32(border), 8.0f);
  ImGui::SetCursorScreenPos({pos.x + 10.0f, pos.y + 8.0f});
  ImGui::PushTextWrapPos(pos.x + width - 10.0f);
  if (isError) ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
  ImGui::TextUnformatted(status.c_str());
  if (isError) ImGui::PopStyleColor();
  ImGui::PopTextWrapPos();
  ImGui::SetCursorScreenPos(pos);
  ImGui::Dummy({width, height});
}

bool CustomViewTab::exportCurrentView(const std::filesystem::path& outputPath) {
  try {
    absorbCreatedPlots();
    const std::string serialized = buildDocumentJson();
    if (outputPath.has_parent_path()) std::filesystem::create_directories(outputPath.parent_path());
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("could not write " + outputPath.string());
    output << serialized;
    output.close();
    copyDocument(document, serialized);
    std::error_code error;
    loadedAt = std::filesystem::last_write_time(outputPath, error);
    dirty = false;
    pathLoaded = true;
    path.fill('\0');
    const size_t count = std::min(outputPath.string().size(), path.size() - 1);
    std::memcpy(path.data(), outputPath.string().data(), count);
    status = "Exported current view to " + outputPath.string();
    return true;
  } catch (const std::exception& error) {
    status = std::string("View export failed: ") + error.what();
    return false;
  }
}

void CustomViewTab::exportSignalCatalog() {
  if (!arena) return;
  try {
    Json catalog = {
        {"schemaVersion", 1}, {"arenaGeneration", arena->generation}, {"messages", Json::array()}};
    for (uint32_t id : arena->validIds) {
      Message* message = id < arena->messages.size() ? arena->messages[id] : nullptr;
      if (!message) continue;
      Json entry = {{"messageId", id},
                    {"messageIdHex", "0x"},
                    {"name", message->name},
                    {"signals", Json::array()}};
      char hex[16]{};
      std::snprintf(hex, sizeof(hex), "0x%03X", id);
      entry["messageIdHex"] = hex;
      for (uint32_t index = 0; index < message->signalCount; ++index) {
        Signal* signal = message->signals[index];
        if (!signal) continue;
        entry["signals"].push_back({{"index", index},
                                    {"name", signal->name},
                                    {"unit", signal->unit},
                                    {"minimum", signal->min},
                                    {"maximum", signal->max}});
      }
      catalog["messages"].push_back(std::move(entry));
    }
    const std::filesystem::path outputPath = "views/photon-signal-catalog.json";
    std::filesystem::create_directories(outputPath.parent_path());
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    output << catalog.dump(2);
    status = "Exported agent/MCP signal context to " + outputPath.string();
  } catch (const std::exception& error) {
    status = std::string("Catalog export failed: ") + error.what();
  }
}

void CustomViewTab::draw(ImGuiWindowFlags flags) {
  consumeFileDialog();
  pumpFileDialogRequest();
  bool fileDialogActive = false;
  bool fileDialogPending = false;
  {
    std::lock_guard lock(dialogMutex);
    fileDialogActive = dialogActive;
    fileDialogPending = requestedDialogAction != CustomViewDialogAction::None;
  }
  const bool dialogBusy = fileDialogActive || fileDialogPending;
  if (arena && resolvedGeneration != arena->generation) resolveSources();
  if (autoReload && !dirty && path[0] != '\0') {
    std::error_code error;
    const auto changedAt = std::filesystem::last_write_time(path.data(), error);
    if (!error && changedAt != loadedAt) load();
  }

  if (ImGui::Begin("Custom Views", nullptr, flags)) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false) && !io.WantTextInput && !dialogBusy)
      showFileDialog(CustomViewDialogAction::Open);

    if (ImGui::Button("Add Plot")) {
      absorbBaseline = plotManager().windows.size();
      absorbArmed = true;
      plotManager().requestCreate();
    }
    ImGui::SameLine();
    if (dialogBusy) ImGui::BeginDisabled();
    if (ImGui::Button(dialogBusy ? "Opening Explorer...##import" : "Import View...##import"))
      showFileDialog(CustomViewDialogAction::Open);
    if (dialogBusy) ImGui::EndDisabled();
    ImGui::SameLine();
    const bool hasExportableView = !view.widgets.empty();
    if (dialogBusy || !hasExportableView) ImGui::BeginDisabled();
    if (ImGui::Button(dialogBusy ? "Opening Explorer...##export" : "Export View...##export"))
      showFileDialog(CustomViewDialogAction::SaveAs);
    if (dialogBusy || !hasExportableView) ImGui::EndDisabled();
    if (view.widgets.empty()) {
      ImGui::TextDisabled(
          "Custom Views only shows explicitly selected signals. Import a view or add a plot.");
    }

    if (ImGui::BeginPopupContextWindow("##custom_view_menu")) {
      if (ImGui::MenuItem("Open View...", "Ctrl+O", false, !dialogBusy))
        showFileDialog(CustomViewDialogAction::Open);
      if (ImGui::MenuItem("Reload View", nullptr, false, path[0] != '\0')) load();
      if (ImGui::MenuItem("Export View...", nullptr, false, hasExportableView && !dialogBusy))
        showFileDialog(CustomViewDialogAction::SaveAs);
      if (ImGui::MenuItem("Export Signal Catalog")) exportSignalCatalog();
      ImGui::EndPopup();
    }

    renderStatus();
    ImGui::Separator();
    renderPreview();
  }
  ImGui::End();
  plotManager().drawCreator();
  absorbCreatedPlots();
}

CustomViewTab& customViewTab() {
  static CustomViewTab tab{};
  return tab;
}
