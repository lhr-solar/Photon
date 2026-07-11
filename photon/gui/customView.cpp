#include "customView.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>

#include <algorithm>
#include <cctype>
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

const char* cellGridModeKey(CustomViewCellGridMode mode) {
  return mode == CustomViewCellGridMode::Temperature ? "temperature" : "voltage";
}

CustomViewCellGridMode cellGridModeFromKey(std::string_view key) {
  return key == "temperature" ? CustomViewCellGridMode::Temperature
                              : CustomViewCellGridMode::Voltage;
}

Json cellGridToJson(const CustomViewCellGrid& grid) {
  return {{"title", grid.title},
          {"cols", grid.cols},
          {"rows", grid.rows},
          {"mode", cellGridModeKey(grid.mode)},
          {"voltageMessageId", grid.voltageMessageId},
          {"temperatureMessageId", grid.temperatureMessageId},
          {"statusMessageId", grid.statusMessageId}};
}

const char* watchdogCompareKey(CustomViewWatchdogCompare compare) {
  return compare == CustomViewWatchdogCompare::Above ? "above" : "below";
}

CustomViewWatchdogCompare watchdogCompareFromKey(std::string_view key) {
  return key == "above" ? CustomViewWatchdogCompare::Above : CustomViewWatchdogCompare::Below;
}

Json watchdogToJson(const CustomViewWatchdog& watchdog) {
  return {{"title", watchdog.title},
          {"message", watchdog.message},
          {"unit", watchdog.unit},
          {"comparison", watchdogCompareKey(watchdog.comparison)},
          {"threshold", watchdog.threshold},
          {"hideWhenOk", watchdog.hideWhenOk},
          {"source", sourceToJson(watchdog.source)}};
}

const char* widgetKindKey(CustomViewWidgetKind kind) {
  switch (kind) {
    case CustomViewWidgetKind::CellGrid:
      return "cell-grid";
    case CustomViewWidgetKind::Watchdog:
      return "watchdog";
    case CustomViewWidgetKind::Plot:
    default:
      return "plot";
  }
}

Json widgetToJson(const CustomViewWidget& widget) {
  Json value = {{"id", widget.id},
                {"kind", widgetKindKey(widget.kind)},
                {"rect",
                 {{"x", widget.rect.x},
                  {"y", widget.rect.y},
                  {"w", widget.rect.width},
                  {"h", widget.rect.height}}}};
  if (widget.kind == CustomViewWidgetKind::CellGrid)
    value["cellGrid"] = cellGridToJson(widget.cellGrid);
  else if (widget.kind == CustomViewWidgetKind::Watchdog)
    value["watchdog"] = watchdogToJson(widget.watchdog);
  else
    value["plot"] = plotToJson(widget.plot);
  return value;
}

const char* widgetTitle(const CustomViewWidget& widget) {
  if (widget.kind == CustomViewWidgetKind::CellGrid) return widget.cellGrid.title.c_str();
  if (widget.kind == CustomViewWidgetKind::Watchdog) return widget.watchdog.title.c_str();
  return widget.plot.title.c_str();
}

uint32_t findSignalIndex(Message* message, std::string_view signalName) {
  if (!message || signalName.empty()) return SIGNAL_MAX;
  for (uint32_t index = 0; index < message->signalCount; ++index) {
    if (message->signals[index] && message->signals[index]->name == signalName) return index;
  }
  return SIGNAL_MAX;
}

Message* messageOrNull(Arena* arena, uint32_t messageId) {
  if (!arena || messageId >= arena->messages.size()) return nullptr;
  return arena->messages[messageId];
}

bool readSignalSeries(Arena* arena, uint32_t messageId, uint32_t signalIndex,
                      const double*& values, int& count) {
  values = nullptr;
  count = 0;
  if (!arena || signalIndex >= SIGNAL_MAX) return false;
  void* data = nullptr;
  uint32_t bytes = 0;
  arena->read(messageId, signalIndex, &data, &bytes);
  const int samples = static_cast<int>(bytes / sizeof(double));
  if (samples <= 0 || !data) return false;
  values = static_cast<const double*>(data);
  count = samples;
  return true;
}

bool readLatestSignal(Arena* arena, uint32_t messageId, uint32_t signalIndex, double& out) {
  const double* values = nullptr;
  int count = 0;
  if (!readSignalSeries(arena, messageId, signalIndex, values, count)) return false;
  out = values[count - 1];
  return true;
}

Json panelToJson(const CustomViewDefinition& panel) {
  Json widgets = Json::array();
  for (const auto& widget : panel.widgets) widgets.push_back(widgetToJson(widget));
  return {{"id", panel.id.empty() ? "panel" : panel.id},
          {"name", panel.name.empty() ? "Panel" : panel.name},
          {"layout",
           {{"columns", panel.columns}, {"rowHeight", panel.rowHeight}, {"gap", panel.gap}}},
          {"widgets", std::move(widgets)}};
}

// Densify coarse grids so drag/resize steps are smaller without changing on-screen layout much.
void densifyPanelGrid(CustomViewDefinition& panel) {
  constexpr int kTargetColumns = 48;
  if (panel.columns > 0 && panel.columns < kTargetColumns && kTargetColumns % panel.columns == 0) {
    const int factor = kTargetColumns / panel.columns;
    for (auto& widget : panel.widgets) {
      widget.rect.x *= factor;
      widget.rect.width *= factor;
    }
    panel.columns = kTargetColumns;
  }

  constexpr float kTargetRowHeight = 48.0f;
  constexpr int kMaxHeight = 48;
  while (panel.rowHeight > kTargetRowHeight * 1.51f) {
    bool canSplit = true;
    for (const auto& widget : panel.widgets) {
      if (widget.rect.height * 2 > kMaxHeight) {
        canSplit = false;
        break;
      }
    }
    if (!canSplit) break;
    for (auto& widget : panel.widgets) {
      widget.rect.y *= 2;
      widget.rect.height = std::max(1, widget.rect.height * 2);
    }
    panel.rowHeight *= 0.5f;
  }
  panel.rowHeight = std::clamp(panel.rowHeight, 48.0f, 1200.0f);
  panel.gap = std::clamp(panel.gap, 0.0f, 64.0f);
}

CustomViewDefinition parsePanelJson(const Json& root, int& plotId) {
  CustomViewDefinition next{};
  next.schemaVersion = 1;
  next.id = root.value("id", "panel-" + std::to_string(plotId));
  next.name = root.value("name", "Panel");
  const Json layout = root.value("layout", Json::object());
  next.columns = std::clamp(layout.value("columns", 48), 1, 48);
  next.rowHeight = std::clamp(layout.value("rowHeight", 48.0f), 48.0f, 1200.0f);
  next.gap = std::clamp(layout.value("gap", 8.0f), 0.0f, 64.0f);
  std::unordered_set<std::string> ids;
  for (const Json& widgetJson : root.value("widgets", Json::array())) {
    const std::string kind = widgetJson.value("kind", "");
    if (kind != "plot" && kind != "cell-grid" && kind != "watchdog") continue;
    CustomViewWidget widget{};
    widget.id = widgetJson.value("id", "widget-" + std::to_string(plotId));
    if (!ids.insert(widget.id).second)
      throw std::runtime_error("duplicate widget id: " + widget.id);
    const Json rect = widgetJson.value("rect", Json::object());
    widget.rect.x = std::max(0, rect.value("x", 0));
    widget.rect.y = std::max(0, rect.value("y", 0));
    widget.rect.width = std::clamp(rect.value("w", next.columns), 1, next.columns);
    widget.rect.height = std::clamp(rect.value("h", 6), 1, 48);
    if (widget.rect.x + widget.rect.width > next.columns)
      throw std::runtime_error("widget exceeds layout columns: " + widget.id);

    if (kind == "cell-grid") {
      widget.kind = CustomViewWidgetKind::CellGrid;
      const Json gridJson = widgetJson.at("cellGrid");
      widget.cellGrid.title = gridJson.value("title", widget.id);
      widget.cellGrid.cols = std::clamp(gridJson.value("cols", 8), 1, kCellGridCapacity);
      widget.cellGrid.rows = std::clamp(gridJson.value("rows", 4), 1, kCellGridCapacity);
      if (widget.cellGrid.cols * widget.cellGrid.rows > kCellGridCapacity)
        throw std::runtime_error(widget.id + " cell-grid exceeds 32 cells");
      widget.cellGrid.mode = cellGridModeFromKey(gridJson.value("mode", "voltage"));
      if (gridJson.find("voltageMessageId") != gridJson.end())
        widget.cellGrid.voltageMessageId = parseMessageId(gridJson.at("voltageMessageId"));
      if (gridJson.find("temperatureMessageId") != gridJson.end())
        widget.cellGrid.temperatureMessageId = parseMessageId(gridJson.at("temperatureMessageId"));
      if (gridJson.find("statusMessageId") != gridJson.end())
        widget.cellGrid.statusMessageId = parseMessageId(gridJson.at("statusMessageId"));
      next.widgets.push_back(std::move(widget));
      continue;
    }

    if (kind == "watchdog") {
      widget.kind = CustomViewWidgetKind::Watchdog;
      const Json dogJson = widgetJson.at("watchdog");
      widget.watchdog.title = dogJson.value("title", widget.id);
      widget.watchdog.message = dogJson.value("message", "Signal out of range");
      widget.watchdog.unit = dogJson.value("unit", "");
      widget.watchdog.comparison = watchdogCompareFromKey(dogJson.value("comparison", "below"));
      widget.watchdog.threshold = dogJson.value("threshold", 0.0);
      widget.watchdog.hideWhenOk = dogJson.value("hideWhenOk", true);
      const Json sourceJson = dogJson.value("source", Json::object());
      if (sourceJson.find("messageId") != sourceJson.end())
        widget.watchdog.source.messageId = parseMessageId(sourceJson.at("messageId"));
      widget.watchdog.source.messageName = sourceJson.value("messageName", "");
      widget.watchdog.source.signalName = sourceJson.value("signalName", "");
      widget.watchdog.source.signalIndex = sourceJson.value("signalIndex", SIGNAL_MAX);
      widget.watchdog.source.assigned = true;
      next.widgets.push_back(std::move(widget));
      continue;
    }

    widget.kind = CustomViewWidgetKind::Plot;
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
  if (next.widgets.size() > 128) throw std::runtime_error("panel exceeds 128 widget limit");
  densifyPanelGrid(next);
  return next;
}

constexpr float kTitleBarHeight = 28.0f;
constexpr float kResizeGrip = 20.0f;
constexpr int kExtraCanvasRows = 6;
}  // namespace

CustomViewDefinition& CustomViewTab::activeView() {
  ensurePanels();
  activePanel = std::clamp(activePanel, 0, static_cast<int>(panels.size()) - 1);
  return panels[static_cast<size_t>(activePanel)];
}

const CustomViewDefinition& CustomViewTab::activeView() const {
  return panels[static_cast<size_t>(
      std::clamp(activePanel, 0, std::max(0, static_cast<int>(panels.size()) - 1)))];
}

void CustomViewTab::ensurePanels() {
  if (!panels.empty()) return;
  CustomViewDefinition panel{};
  panel.id = "panel-1";
  panel.name = "Panel 1";
  panels.push_back(std::move(panel));
  activePanel = 0;
  nextPanelId = 2;
}

void CustomViewTab::setActivePanel(int index) {
  if (panels.empty()) return;
  const int next = std::clamp(index, 0, static_cast<int>(panels.size()) - 1);
  if (next == activePanel) return;
  activePanel = next;
  forceSelectPanel = true;
  interactMode = CustomViewInteractMode::None;
  interactWidgetId.clear();
}

void CustomViewTab::addPanel() {
  ensurePanels();
  CustomViewDefinition panel{};
  panel.id = "panel-" + std::to_string(nextPanelId);
  panel.name = "Panel " + std::to_string(nextPanelId);
  ++nextPanelId;
  panels.push_back(std::move(panel));
  activePanel = static_cast<int>(panels.size()) - 1;
  forceSelectPanel = true;
  interactMode = CustomViewInteractMode::None;
  interactWidgetId.clear();
  syncDocumentFromView("Added panel.", pathLoaded || path[0] != '\0');
}

void CustomViewTab::removePanel(int index) {
  if (panels.size() <= 1) return;
  if (index < 0 || index >= static_cast<int>(panels.size())) return;
  panels.erase(panels.begin() + index);
  if (activePanel >= static_cast<int>(panels.size()))
    activePanel = static_cast<int>(panels.size()) - 1;
  forceSelectPanel = true;
  interactMode = CustomViewInteractMode::None;
  interactWidgetId.clear();
  syncDocumentFromView("Removed panel.", pathLoaded || path[0] != '\0');
}

void CustomViewTab::renderPanelBar() {
  ensurePanels();
  if (ImGui::BeginTabBar("##custom_view_panels", ImGuiTabBarFlags_FittingPolicyScroll)) {
    int pendingRemove = -1;
    for (int i = 0; i < static_cast<int>(panels.size()); ++i) {
      ImGui::PushID(i);
      bool open = true;
      // Only force-select after load/add/remove — applying SetSelected every frame snaps
      // the user back to the previously active tab when they try to switch.
      ImGuiTabItemFlags flags = 0;
      if (forceSelectPanel && i == activePanel) flags |= ImGuiTabItemFlags_SetSelected;
      const bool closable = panels.size() > 1;
      const std::string label =
          panels[static_cast<size_t>(i)].name + "###" + panels[static_cast<size_t>(i)].id;
      if (ImGui::BeginTabItem(label.c_str(), closable ? &open : nullptr, flags)) {
        activePanel = i;
        ImGui::EndTabItem();
      }
      if (closable && !open) pendingRemove = i;
      ImGui::PopID();
    }
    forceSelectPanel = false;
    if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
      addPanel();
    ImGui::EndTabBar();
    if (pendingRemove >= 0) removePanel(pendingRemove);
  }

  CustomViewDefinition& panel = activeView();
  ImGui::SetNextItemWidth(220.0f);
  std::array<char, 128> nameBuf{};
  const size_t nameCount = std::min(panel.name.size(), nameBuf.size() - 1);
  std::memcpy(nameBuf.data(), panel.name.data(), nameCount);
  if (ImGui::InputText("##panel_name", nameBuf.data(), nameBuf.size())) {
    panel.name = nameBuf.data();
    if (panel.name.empty()) panel.name = "Panel";
  }
  if (ImGui::IsItemDeactivatedAfterEdit())
    syncDocumentFromView("Renamed panel.", pathLoaded || path[0] != '\0');
  ImGui::SameLine();
  ImGui::TextDisabled("Active panel name");
}

void CustomViewTab::init(Arena* arenaTarget, SDL_Window* windowTarget) {
  if (arena == arenaTarget && window == windowTarget) return;
  arena = arenaTarget;
  window = windowTarget;
  resolvedGeneration = arena ? arena->generation : 0;
  document.fill('\0');
  panels.clear();
  activePanel = 0;
  dirty = false;
  pathLoaded = false;
  absorbArmed = false;
  absorbBaseline = 0;
  interactMode = CustomViewInteractMode::None;
  interactWidgetId.clear();
  if (std::filesystem::exists(path.data()))
    load();
  else {
    ensurePanels();
    status = "No custom view loaded. Add a panel/plot or press Ctrl+O to open a view.";
  }
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
  panels.clear();
  activePanel = 0;
  nextPanelId = 2;
  nextWidgetPlotId = 10000;
  ensurePanels();
  syncDocumentFromView("New empty workspace created. Add panels or plots.", false);
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
    if (statusDetail.empty())
      status = "Loaded and validated " + filePath.string();
    else
      status = "Loaded " + filePath.string() + " — " + status;
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

    int plotId = 10000;
    std::vector<CustomViewDefinition> nextPanels{};
    if (root.find("panels") != root.end() && root["panels"].is_array() && !root["panels"].empty()) {
      for (const Json& panelJson : root["panels"])
        nextPanels.push_back(parsePanelJson(panelJson, plotId));
    } else {
      nextPanels.push_back(parsePanelJson(root, plotId));
    }
    if (nextPanels.size() > 32) throw std::runtime_error("workspace exceeds 32 panel limit");

    panels = std::move(nextPanels);
    activePanel = std::clamp(root.value("activePanel", 0), 0, static_cast<int>(panels.size()) - 1);
    forceSelectPanel = true;
    nextWidgetPlotId = plotId;
    nextPanelId = static_cast<int>(panels.size()) + 1;
    for (const auto& panel : panels) {
      if (panel.id.rfind("panel-", 0) == 0) {
        try {
          nextPanelId = std::max(nextPanelId, std::stoi(panel.id.substr(6)) + 1);
        } catch (...) {
        }
      }
    }
    resolveSources();
    dirty = true;
    // resolveSources() already sets status for DBC bind results; keep a short
    // structural prefix when everything resolved.
    if (status.find("unresolved") == std::string::npos) {
      size_t widgetCount = 0;
      for (const auto& panel : panels) widgetCount += panel.widgets.size();
      status = "Config is valid: " + std::to_string(panels.size()) + " panel(s), " +
               std::to_string(widgetCount) + " widget(s). " + status;
    }
    return true;
  } catch (const std::exception& error) {
    status = std::string("Validation failed: ") + error.what();
    return false;
  }
}

void CustomViewTab::resolveCellGrid(CustomViewCellGrid& grid) {
  grid.resolved = false;
  grid.voltageTapIdx = SIGNAL_MAX;
  grid.voltageDataIdx = SIGNAL_MAX;
  grid.voltageFaultIdx = SIGNAL_MAX;
  grid.voltageAgeIdx = SIGNAL_MAX;
  grid.temperatureTapIdx = SIGNAL_MAX;
  grid.temperatureDataIdx = SIGNAL_MAX;
  grid.temperatureFaultIdx = SIGNAL_MAX;
  grid.temperatureAgeIdx = SIGNAL_MAX;
  grid.statusPackVoltageIdx = SIGNAL_MAX;
  grid.statusAvgTempIdx = SIGNAL_MAX;
  grid.statusFaultIdx = SIGNAL_MAX;
  if (!arena) return;

  Message* voltageMsg = messageOrNull(arena, grid.voltageMessageId);
  Message* temperatureMsg = messageOrNull(arena, grid.temperatureMessageId);
  Message* statusMsg = messageOrNull(arena, grid.statusMessageId);

  grid.voltageTapIdx = findSignalIndex(voltageMsg, "BPS_Tap_idx");
  grid.voltageDataIdx = findSignalIndex(voltageMsg, "BPS_Voltage_Tap_Data");
  grid.voltageFaultIdx = findSignalIndex(voltageMsg, "BPS_Voltage_Tap_Fault");
  grid.voltageAgeIdx = findSignalIndex(voltageMsg, "BPS_Voltage_Tap_Age");

  grid.temperatureTapIdx = findSignalIndex(temperatureMsg, "BPS_Tap_idx");
  grid.temperatureDataIdx = findSignalIndex(temperatureMsg, "BPS_Temperature_Tap_Data");
  grid.temperatureFaultIdx = findSignalIndex(temperatureMsg, "BPS_Temperature_Tap_Fault");
  grid.temperatureAgeIdx = findSignalIndex(temperatureMsg, "BPS_Temperature_Tap_Age");

  grid.statusPackVoltageIdx = findSignalIndex(statusMsg, "Main_Battery_Voltage");
  grid.statusAvgTempIdx = findSignalIndex(statusMsg, "Main_Battery_Avg_Temperature");
  grid.statusFaultIdx = findSignalIndex(statusMsg, "BPS_Fault");

  grid.resolved = grid.voltageTapIdx != SIGNAL_MAX && grid.voltageDataIdx != SIGNAL_MAX &&
                  grid.temperatureTapIdx != SIGNAL_MAX && grid.temperatureDataIdx != SIGNAL_MAX;
}

void CustomViewTab::updateCellGridSnapshot(CustomViewCellGrid& grid) {
  for (auto& cell : grid.cells) cell = {};
  grid.packVoltage = 0.0;
  grid.packAvgTemp = 0.0;
  grid.packFault = 0.0;
  grid.hasPackVoltage = false;
  grid.hasPackAvgTemp = false;
  grid.hasPackFault = false;
  if (!arena || !grid.resolved) return;

  const double* voltageTap = nullptr;
  const double* voltageData = nullptr;
  const double* voltageFault = nullptr;
  const double* voltageAge = nullptr;
  int voltageTapCount = 0;
  int voltageDataCount = 0;
  int voltageFaultCount = 0;
  int voltageAgeCount = 0;
  readSignalSeries(arena, grid.voltageMessageId, grid.voltageTapIdx, voltageTap, voltageTapCount);
  readSignalSeries(arena, grid.voltageMessageId, grid.voltageDataIdx, voltageData, voltageDataCount);
  readSignalSeries(arena, grid.voltageMessageId, grid.voltageFaultIdx, voltageFault,
                   voltageFaultCount);
  readSignalSeries(arena, grid.voltageMessageId, grid.voltageAgeIdx, voltageAge, voltageAgeCount);
  const int voltageSamples =
      std::min(voltageTapCount, voltageDataCount);
  std::array<bool, kCellGridCapacity> voltageFilled{};
  for (int i = voltageSamples - 1; i >= 0; --i) {
    const int tap = static_cast<int>(std::lround(voltageTap[i]));
    if (tap < 0 || tap >= kCellGridCapacity || voltageFilled[static_cast<size_t>(tap)]) continue;
    auto& cell = grid.cells[static_cast<size_t>(tap)];
    cell.voltage = voltageData[i];
    cell.hasVoltage = true;
    if (i < voltageFaultCount) cell.voltageFault = voltageFault[i];
    if (i < voltageAgeCount) cell.voltageAgeMs = voltageAge[i];
    voltageFilled[static_cast<size_t>(tap)] = true;
  }

  const double* temperatureTap = nullptr;
  const double* temperatureData = nullptr;
  const double* temperatureFault = nullptr;
  const double* temperatureAge = nullptr;
  int temperatureTapCount = 0;
  int temperatureDataCount = 0;
  int temperatureFaultCount = 0;
  int temperatureAgeCount = 0;
  readSignalSeries(arena, grid.temperatureMessageId, grid.temperatureTapIdx, temperatureTap,
                   temperatureTapCount);
  readSignalSeries(arena, grid.temperatureMessageId, grid.temperatureDataIdx, temperatureData,
                   temperatureDataCount);
  readSignalSeries(arena, grid.temperatureMessageId, grid.temperatureFaultIdx, temperatureFault,
                   temperatureFaultCount);
  readSignalSeries(arena, grid.temperatureMessageId, grid.temperatureAgeIdx, temperatureAge,
                   temperatureAgeCount);
  const int temperatureSamples = std::min(temperatureTapCount, temperatureDataCount);
  std::array<bool, kCellGridCapacity> temperatureFilled{};
  for (int i = temperatureSamples - 1; i >= 0; --i) {
    const int tap = static_cast<int>(std::lround(temperatureTap[i]));
    if (tap < 0 || tap >= kCellGridCapacity || temperatureFilled[static_cast<size_t>(tap)])
      continue;
    auto& cell = grid.cells[static_cast<size_t>(tap)];
    cell.temperature = temperatureData[i];
    cell.hasTemperature = true;
    if (i < temperatureFaultCount) cell.temperatureFault = temperatureFault[i];
    if (i < temperatureAgeCount) cell.temperatureAgeMs = temperatureAge[i];
    temperatureFilled[static_cast<size_t>(tap)] = true;
  }

  grid.hasPackVoltage =
      readLatestSignal(arena, grid.statusMessageId, grid.statusPackVoltageIdx, grid.packVoltage);
  grid.hasPackAvgTemp =
      readLatestSignal(arena, grid.statusMessageId, grid.statusAvgTempIdx, grid.packAvgTemp);
  grid.hasPackFault =
      readLatestSignal(arena, grid.statusMessageId, grid.statusFaultIdx, grid.packFault);
}

namespace {
ImVec4 lerp4(ImVec4 a, ImVec4 b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t);
}

ImVec4 voltageRamp(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  // deep teal -> electric cyan -> lime -> amber -> hot magenta
  if (t < 0.25f) return lerp4({0.05f, 0.28f, 0.42f, 1.0f}, {0.08f, 0.78f, 0.92f, 1.0f}, t / 0.25f);
  if (t < 0.50f)
    return lerp4({0.08f, 0.78f, 0.92f, 1.0f}, {0.35f, 0.95f, 0.35f, 1.0f}, (t - 0.25f) / 0.25f);
  if (t < 0.75f)
    return lerp4({0.35f, 0.95f, 0.35f, 1.0f}, {1.0f, 0.78f, 0.12f, 1.0f}, (t - 0.50f) / 0.25f);
  return lerp4({1.0f, 0.78f, 0.12f, 1.0f}, {1.0f, 0.28f, 0.55f, 1.0f}, (t - 0.75f) / 0.25f);
}

ImVec4 temperatureRamp(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  // ice blue -> violet -> orange -> crimson
  if (t < 0.33f) return lerp4({0.25f, 0.45f, 1.0f, 1.0f}, {0.72f, 0.28f, 1.0f, 1.0f}, t / 0.33f);
  if (t < 0.66f)
    return lerp4({0.72f, 0.28f, 1.0f, 1.0f}, {1.0f, 0.55f, 0.12f, 1.0f}, (t - 0.33f) / 0.33f);
  return lerp4({1.0f, 0.55f, 0.12f, 1.0f}, {1.0f, 0.12f, 0.22f, 1.0f}, (t - 0.66f) / 0.34f);
}

void drawPill(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 fill, ImU32 border, ImU32 text,
              const char* label) {
  const float rounding = (max.y - min.y) * 0.5f;
  draw->AddRectFilled(min, max, fill, rounding);
  draw->AddRect(min, max, border, rounding, 0, 1.2f);
  const ImVec2 size = ImGui::CalcTextSize(label);
  draw->AddText({min.x + (max.x - min.x - size.x) * 0.5f, min.y + (max.y - min.y - size.y) * 0.5f},
                text, label);
}

void drawCellTile(ImDrawList* draw, ImVec2 min, ImVec2 max, ImVec4 base, bool hasValue, bool faulted,
                  bool showVoltage, int index, double value, float pulse) {
  const float rounding = 7.0f;
  const ImVec4 empty = {0.12f, 0.14f, 0.18f, 0.85f};
  ImVec4 fill = hasValue ? base : empty;
  if (hasValue) {
    fill.x = std::min(1.0f, fill.x * 1.15f + 0.05f);
    fill.y = std::min(1.0f, fill.y * 1.10f + 0.04f);
    fill.z = std::min(1.0f, fill.z * 1.10f + 0.04f);
  }

  // Soft outer glow for live cells.
  if (hasValue) {
    ImVec4 glow = fill;
    glow.w = 0.22f + 0.10f * pulse;
    const float inflate = 2.5f + pulse * 1.5f;
    draw->AddRectFilled({min.x - inflate, min.y - inflate}, {max.x + inflate, max.y + inflate},
                        PhotonUi::colorU32(glow), rounding + 2.0f);
  }

  const ImVec4 top = hasValue ? lerp4(fill, {1.0f, 1.0f, 1.0f, 1.0f}, 0.22f) : empty;
  draw->AddRectFilled(min, max, PhotonUi::colorU32(fill), rounding);
  if (hasValue) {
    const float midY = min.y + (max.y - min.y) * 0.55f;
    draw->AddRectFilled(min, {max.x, midY}, PhotonUi::colorU32(PhotonUi::withAlpha(top, 0.55f)),
                        rounding);
  }

  ImVec4 borderCol = hasValue ? lerp4(fill, {1.0f, 1.0f, 1.0f, 1.0f}, 0.45f)
                              : ImVec4{0.28f, 0.32f, 0.38f, 0.9f};
  borderCol.w = 0.95f;
  float borderThickness = 1.4f;
  if (faulted) {
    borderCol = {1.0f, 0.20f + 0.25f * pulse, 0.28f, 1.0f};
    borderThickness = 2.4f + pulse;
  }
  draw->AddRect(min, max, PhotonUi::colorU32(borderCol), rounding, 0, borderThickness);

  // Specular strip.
  if (hasValue) {
    const float stripH = std::max(3.0f, (max.y - min.y) * 0.18f);
    draw->AddRectFilled({min.x + 3.0f, min.y + 3.0f}, {max.x - 3.0f, min.y + 3.0f + stripH},
                        PhotonUi::colorU32({1.0f, 1.0f, 1.0f, 0.16f}), 3.0f);
  }

  char indexBuf[16]{};
  char valueBuf[32]{};
  std::snprintf(indexBuf, sizeof(indexBuf), "#%02d", index);
  if (hasValue) {
    if (showVoltage)
      std::snprintf(valueBuf, sizeof(valueBuf), "%.3f V", value);
    else
      std::snprintf(valueBuf, sizeof(valueBuf), "%.1f C", value);
  } else {
    std::snprintf(valueBuf, sizeof(valueBuf), "--");
  }

  const float pad = 5.0f;
  draw->AddText({min.x + pad, min.y + pad},
                PhotonUi::colorU32(hasValue ? ImVec4{1.0f, 1.0f, 1.0f, 0.72f}
                                            : ImVec4{0.55f, 0.58f, 0.64f, 0.85f}),
                indexBuf);

  const ImVec2 valueSize = ImGui::CalcTextSize(valueBuf);
  const ImVec2 valuePos{min.x + (max.x - min.x - valueSize.x) * 0.5f,
                        min.y + (max.y - min.y - valueSize.y) * 0.55f};
  // Soft text shadow for punch.
  draw->AddText({valuePos.x + 1.0f, valuePos.y + 1.0f},
                PhotonUi::colorU32({0.0f, 0.0f, 0.0f, 0.55f}), valueBuf);
  draw->AddText(valuePos,
                PhotonUi::colorU32(hasValue ? ImVec4{1.0f, 1.0f, 1.0f, 1.0f}
                                            : ImVec4{0.62f, 0.66f, 0.72f, 0.9f}),
                valueBuf);
}
}  // namespace

void CustomViewTab::renderCellGrid(CustomViewWidget& widget) {
  CustomViewCellGrid& grid = widget.cellGrid;
  updateCellGridSnapshot(grid);

  const PhotonUi::Palette palette = PhotonUi::palette();
  const float pulse =
      0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 5.5f);

  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 headerOrigin = ImGui::GetCursorScreenPos();
  const float headerH = 34.0f;
  const float availX = std::max(120.0f, ImGui::GetContentRegionAvail().x);
  draw->AddRectFilled(headerOrigin, {headerOrigin.x + availX, headerOrigin.y + headerH},
                      PhotonUi::colorU32(PhotonUi::withAlpha(palette.raised, 0.92f)), 8.0f);
  draw->AddRectFilledMultiColor(
      headerOrigin, {headerOrigin.x + availX, headerOrigin.y + headerH},
      PhotonUi::colorU32({0.15f, 0.55f, 0.95f, 0.18f}), PhotonUi::colorU32({0.95f, 0.25f, 0.65f, 0.14f}),
      PhotonUi::colorU32({0.95f, 0.25f, 0.65f, 0.04f}), PhotonUi::colorU32({0.15f, 0.55f, 0.95f, 0.04f}));

  // Mode pills
  const float pillW = 92.0f;
  const float pillH = 22.0f;
  const float pillY = headerOrigin.y + (headerH - pillH) * 0.5f;
  ImVec2 voltMin{headerOrigin.x + 10.0f, pillY};
  ImVec2 voltMax{voltMin.x + pillW, pillY + pillH};
  ImVec2 tempMin{voltMax.x + 8.0f, pillY};
  ImVec2 tempMax{tempMin.x + pillW, pillY + pillH};

  ImGui::SetCursorScreenPos(voltMin);
  if (ImGui::InvisibleButton("##mode_voltage", {pillW, pillH}))
    grid.mode = CustomViewCellGridMode::Voltage;
  ImGui::SetCursorScreenPos(tempMin);
  if (ImGui::InvisibleButton("##mode_temperature", {pillW, pillH}))
    grid.mode = CustomViewCellGridMode::Temperature;

  const bool voltageMode = grid.mode == CustomViewCellGridMode::Voltage;
  drawPill(draw, voltMin, voltMax,
           PhotonUi::colorU32(voltageMode ? ImVec4{0.10f, 0.72f, 0.95f, 0.95f}
                                          : ImVec4{0.18f, 0.20f, 0.24f, 0.9f}),
           PhotonUi::colorU32(voltageMode ? ImVec4{0.65f, 0.95f, 1.0f, 1.0f}
                                          : PhotonUi::withAlpha(palette.border, 0.7f)),
           PhotonUi::colorU32(voltageMode ? ImVec4{0.02f, 0.08f, 0.12f, 1.0f} : palette.muted),
           "Voltage");
  drawPill(draw, tempMin, tempMax,
           PhotonUi::colorU32(!voltageMode ? ImVec4{1.0f, 0.42f, 0.28f, 0.95f}
                                           : ImVec4{0.18f, 0.20f, 0.24f, 0.9f}),
           PhotonUi::colorU32(!voltageMode ? ImVec4{1.0f, 0.75f, 0.45f, 1.0f}
                                           : PhotonUi::withAlpha(palette.border, 0.7f)),
           PhotonUi::colorU32(!voltageMode ? ImVec4{0.12f, 0.04f, 0.02f, 1.0f} : palette.muted),
           "Temperature");

  char packV[32] = "-- V";
  char packT[32] = "-- C";
  char faultText[32] = "--";
  if (grid.hasPackVoltage) std::snprintf(packV, sizeof(packV), "%.2f V", grid.packVoltage);
  if (grid.hasPackAvgTemp) std::snprintf(packT, sizeof(packT), "%.1f C", grid.packAvgTemp);
  if (grid.hasPackFault)
    std::snprintf(faultText, sizeof(faultText), "%d", static_cast<int>(grid.packFault));

  char statusBuf[128]{};
  if (grid.hasPackVoltage || grid.hasPackAvgTemp || grid.hasPackFault)
    std::snprintf(statusBuf, sizeof(statusBuf), "PACK  %s   %s   FAULT %s", packV, packT, faultText);
  else
    std::snprintf(statusBuf, sizeof(statusBuf),
                  grid.resolved ? "Waiting for BPS frames..." : "Unresolved BPS messages");

  const bool hasFault = grid.hasPackFault && grid.packFault != 0.0;
  const ImVec2 statusSize = ImGui::CalcTextSize(statusBuf);
  const float statusX = headerOrigin.x + availX - statusSize.x - 14.0f;
  draw->AddText({statusX, headerOrigin.y + (headerH - statusSize.y) * 0.5f},
                PhotonUi::colorU32(hasFault ? ImVec4{1.0f, 0.35f + 0.25f * pulse, 0.35f, 1.0f}
                                            : ImVec4{0.85f, 0.92f, 1.0f, 0.95f}),
                statusBuf);

  ImGui::SetCursorScreenPos({headerOrigin.x, headerOrigin.y + headerH + 8.0f});

  // Legend strip
  const ImVec2 legendOrigin = ImGui::GetCursorScreenPos();
  const float legendH = 10.0f;
  const float legendW = availX;
  for (int i = 0; i < 64; ++i) {
    const float t0 = static_cast<float>(i) / 64.0f;
    const float t1 = static_cast<float>(i + 1) / 64.0f;
    const ImVec4 c = voltageMode ? voltageRamp(t0) : temperatureRamp(t0);
    draw->AddRectFilled({legendOrigin.x + legendW * t0, legendOrigin.y},
                        {legendOrigin.x + legendW * t1, legendOrigin.y + legendH},
                        PhotonUi::colorU32(c));
  }
  draw->AddRect(legendOrigin, {legendOrigin.x + legendW, legendOrigin.y + legendH},
                PhotonUi::colorU32(PhotonUi::withAlpha(palette.border, 0.8f)), 2.0f);
  ImGui::Dummy({availX, legendH + 8.0f});

  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float gap = 6.0f;
  const float cellW =
      std::max(8.0f, (avail.x - gap * static_cast<float>(grid.cols - 1)) / static_cast<float>(grid.cols));
  const float cellH =
      std::max(8.0f, (avail.y - gap * static_cast<float>(grid.rows - 1)) / static_cast<float>(grid.rows));
  const ImVec2 origin = ImGui::GetCursorScreenPos();

  constexpr float kVoltageMin = 3.0f;
  constexpr float kVoltageMax = 4.2f;
  constexpr float kTempMin = 15.0f;
  constexpr float kTempMax = 60.0f;

  const int cellCount = std::min(kCellGridCapacity, grid.cols * grid.rows);
  for (int index = 0; index < cellCount; ++index) {
    const int row = index / grid.cols;
    const int col = index % grid.cols;
    const ImVec2 min(origin.x + static_cast<float>(col) * (cellW + gap),
                     origin.y + static_cast<float>(row) * (cellH + gap));
    const ImVec2 max(min.x + cellW, min.y + cellH);
    const CustomViewCellSample& cell = grid.cells[static_cast<size_t>(index)];
    const bool hasValue = voltageMode ? cell.hasVoltage : cell.hasTemperature;
    const double value = voltageMode ? cell.voltage : cell.temperature;
    const bool faulted =
        (cell.hasVoltage && cell.voltageFault != 0.0) ||
        (cell.hasTemperature && cell.temperatureFault != 0.0);

    float t = 0.0f;
    if (hasValue) {
      if (voltageMode)
        t = static_cast<float>((value - kVoltageMin) / (kVoltageMax - kVoltageMin));
      else
        t = static_cast<float>((value - kTempMin) / (kTempMax - kTempMin));
    }
    const ImVec4 base = voltageMode ? voltageRamp(t) : temperatureRamp(t);
    drawCellTile(draw, min, max, base, hasValue, faulted, voltageMode, index, value, pulse);
  }

  ImGui::Dummy(ImVec2(avail.x, static_cast<float>(grid.rows) * (cellH + gap) - gap));
}

void CustomViewTab::resolveWatchdog(CustomViewWatchdog& watchdog) {
  watchdog.source.assigned = false;
  if (!arena) return;
  Message* message = messageOrNull(arena, watchdog.source.messageId);
  if (!message && !watchdog.source.messageName.empty()) {
    for (uint32_t id : arena->validIds) {
      Message* candidate = messageOrNull(arena, id);
      if (candidate && candidate->name == watchdog.source.messageName) {
        message = candidate;
        watchdog.source.messageId = id;
        break;
      }
    }
  }
  if (!message) return;
  uint32_t index = SIGNAL_MAX;
  if (!watchdog.source.signalName.empty())
    index = findSignalIndex(message, watchdog.source.signalName);
  if (index == SIGNAL_MAX && watchdog.source.signalIndex < message->signalCount &&
      message->signals[watchdog.source.signalIndex])
    index = watchdog.source.signalIndex;
  if (index == SIGNAL_MAX) return;
  watchdog.source.signalIndex = index;
  watchdog.source.messageName = message->name;
  watchdog.source.signalName = message->signals[index]->name;
  if (watchdog.unit.empty() || watchdog.unit == "NULL") {
    const std::string& unit = message->signals[index]->unit;
    if (!unit.empty() && unit != "NULL") watchdog.unit = unit;
  }
  watchdog.source.assigned = true;
}

void CustomViewTab::updateWatchdog(CustomViewWatchdog& watchdog) {
  watchdog.hasValue = false;
  watchdog.tripped = false;
  if (!watchdog.source.assigned) return;
  double value = 0.0;
  if (!readLatestSignal(arena, watchdog.source.messageId, watchdog.source.signalIndex, value))
    return;
  watchdog.latest = value;
  watchdog.hasValue = true;
  watchdog.tripped = watchdog.comparison == CustomViewWatchdogCompare::Above
                         ? value > watchdog.threshold
                         : value < watchdog.threshold;
}

void CustomViewTab::openWatchdogCreator() {
  pendingWatchdog = {};
  pendingWatchdog.title = "Watchdog";
  pendingWatchdog.message = "Signal out of range";
  pendingWatchdog.comparison = CustomViewWatchdogCompare::Below;
  pendingWatchdog.threshold = 100.0;
  pendingWatchdog.hideWhenOk = true;
  watchdogSearch.fill('\0');
  watchdogCreatorFocusSearch = true;
  watchdogCreatorOpen = true;
}

void CustomViewTab::commitWatchdogCreator() {
  if (!pendingWatchdog.source.assigned) return;
  resolveWatchdog(pendingWatchdog);
  if (!pendingWatchdog.source.assigned) return;

  CustomViewDefinition& view = activeView();
  CustomViewWidget widget{};
  widget.kind = CustomViewWidgetKind::Watchdog;
  widget.id = "watchdog-" + std::to_string(nextWidgetPlotId++);
  widget.rect = clampRect({0, findNextRow(), std::min(24, view.columns), 4});
  if (pendingWatchdog.title.empty() || pendingWatchdog.title == "Watchdog") {
    pendingWatchdog.title = pendingWatchdog.source.signalName.empty()
                                ? pendingWatchdog.source.messageName
                                : pendingWatchdog.source.signalName;
  }
  if (pendingWatchdog.message.empty() || pendingWatchdog.message == "Signal out of range") {
    pendingWatchdog.message =
        pendingWatchdog.title +
        (pendingWatchdog.comparison == CustomViewWatchdogCompare::Above ? " too high" : " too low");
  }
  widget.watchdog = pendingWatchdog;
  view.widgets.push_back(std::move(widget));
  syncDocumentFromView("Added watchdog widget.", pathLoaded || path[0] != '\0');
}

void CustomViewTab::renderWatchdogCreator() {
  if (!watchdogCreatorOpen) return;
  plotManager().refreshForArena();

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 center = viewport ? viewport->GetCenter() : ImVec2(0.0f, 0.0f);
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(720.0f, 0.0f), ImGuiCond_Always);
  bool keepOpen = true;
  const ImGuiStyle& theme = ImGui::GetStyle();
  ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.Colors[ImGuiCol_PopupBg]);
  ImGui::PushStyleColor(ImGuiCol_Border, theme.Colors[ImGuiCol_Border]);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  if (!ImGui::Begin("Create Watchdog", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoDocking)) {
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    watchdogCreatorOpen = keepOpen;
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) keepOpen = false;

  if (ImGui::BeginTable("##watchdog_creator", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Config", ImGuiTableColumnFlags_WidthStretch, 0.45f);
    ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_WidthStretch, 0.55f);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::SeparatorText("Watchdog");
    std::array<char, 96> titleBuf{};
    std::array<char, 160> messageBuf{};
    const size_t titleN = std::min(pendingWatchdog.title.size(), titleBuf.size() - 1);
    const size_t messageN = std::min(pendingWatchdog.message.size(), messageBuf.size() - 1);
    std::memcpy(titleBuf.data(), pendingWatchdog.title.data(), titleN);
    std::memcpy(messageBuf.data(), pendingWatchdog.message.data(), messageN);
    if (ImGui::InputText("Title", titleBuf.data(), titleBuf.size()))
      pendingWatchdog.title = titleBuf.data();
    if (ImGui::InputText("Message", messageBuf.data(), messageBuf.size()))
      pendingWatchdog.message = messageBuf.data();

    int compareIndex = pendingWatchdog.comparison == CustomViewWatchdogCompare::Above ? 1 : 0;
    const char* compareItems[] = {"Warn below threshold", "Warn above threshold"};
    if (ImGui::Combo("Condition", &compareIndex, compareItems, 2)) {
      pendingWatchdog.comparison = compareIndex == 1 ? CustomViewWatchdogCompare::Above
                                                     : CustomViewWatchdogCompare::Below;
    }
    float threshold = static_cast<float>(pendingWatchdog.threshold);
    if (ImGui::DragFloat("Threshold", &threshold, 1.0f, -1.0e6f, 1.0e6f, "%.3f"))
      pendingWatchdog.threshold = threshold;
    ImGui::Checkbox("Hide when OK", &pendingWatchdog.hideWhenOk);

    ImGui::Spacing();
    ImGui::SeparatorText("Preview");
    resolveWatchdog(pendingWatchdog);
    updateWatchdog(pendingWatchdog);
    if (!pendingWatchdog.source.assigned)
      ImGui::TextDisabled("Select a signal to monitor.");
    else if (!pendingWatchdog.hasValue)
      ImGui::TextDisabled("Waiting for signal data...");
    else if (pendingWatchdog.tripped)
      ImGui::TextColored(PhotonUi::palette().accent, "TRIPPED  %.3f %s", pendingWatchdog.latest,
                         pendingWatchdog.unit.c_str());
    else
      ImGui::Text("OK  %.3f %s", pendingWatchdog.latest, pendingWatchdog.unit.c_str());

    ImGui::TableSetColumnIndex(1);
    ImGui::SeparatorText("Signal");
    if (watchdogCreatorFocusSearch) {
      ImGui::SetKeyboardFocusHere();
      watchdogCreatorFocusSearch = false;
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##watchdog_search", "Search message, id, or signal",
                             watchdogSearch.data(), watchdogSearch.size());

    const auto& options = plotManager().signalOptions;
    if (ImGui::BeginChild("##watchdog_signals", ImVec2(-1.0f, 320.0f), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
      for (const auto& option : options) {
        bool matches = true;
        if (watchdogSearch[0] != '\0') {
          matches = false;
          const std::string query = watchdogSearch.data();
          std::string hay = option.label;
          std::string needle = query;
          for (char& c : hay) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          for (char& c : needle)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          matches = hay.find(needle) != std::string::npos;
        }
        if (!matches) continue;
        const bool selected = pendingWatchdog.source.assigned &&
                              option.ref.messageId == pendingWatchdog.source.messageId &&
                              option.ref.signalName == pendingWatchdog.source.signalName;
        if (ImGui::Selectable(option.label.c_str(), selected,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
          pendingWatchdog.source = option.ref;
          pendingWatchdog.source.assigned = true;
          if (pendingWatchdog.title.empty() || pendingWatchdog.title == "Watchdog")
            pendingWatchdog.title = option.ref.signalName.empty() ? option.ref.messageName
                                                                  : option.ref.signalName;
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
              pendingWatchdog.source.assigned) {
            commitWatchdogCreator();
            keepOpen = false;
          }
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndChild();
    ImGui::EndTable();
  }

  const bool canCreate = pendingWatchdog.source.assigned;
  if (!canCreate) ImGui::BeginDisabled();
  if (ImGui::Button("Create")) {
    commitWatchdogCreator();
    keepOpen = false;
  }
  if (!canCreate) ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) keepOpen = false;

  watchdogCreatorOpen = keepOpen;
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
}

void CustomViewTab::renderWatchdog(CustomViewWidget& widget) {
  CustomViewWatchdog& dog = widget.watchdog;
  resolveWatchdog(dog);
  updateWatchdog(dog);

  const PhotonUi::Palette palette = PhotonUi::palette();
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = std::max(48.0f, ImGui::GetContentRegionAvail().y);
  ImDrawList* draw = ImGui::GetWindowDrawList();

  if (!dog.source.assigned) {
    draw->AddRectFilled(pos, {pos.x + width, pos.y + height},
                        PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.7f)), 6.0f);
    ImGui::SetCursorScreenPos({pos.x + 10.0f, pos.y + 14.0f});
    ImGui::TextDisabled("Watchdog has no assigned signal.");
  } else if (!dog.hasValue) {
    draw->AddRectFilled(pos, {pos.x + width, pos.y + height},
                        PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.7f)), 6.0f);
    ImGui::SetCursorScreenPos({pos.x + 10.0f, pos.y + 14.0f});
    ImGui::TextDisabled("Waiting for signal data...");
  } else if (dog.tripped) {
    draw->AddRectFilled(pos, {pos.x + width, pos.y + height},
                        PhotonUi::colorU32(PhotonUi::withAlpha(palette.active, 0.85f)), 6.0f);
    draw->AddRect(pos, {pos.x + width, pos.y + height}, PhotonUi::colorU32(palette.accent), 6.0f, 0,
                  2.0f);
    ImGui::SetCursorScreenPos({pos.x + 12.0f, pos.y + 10.0f});
    ImGui::PushTextWrapPos(pos.x + width - 12.0f);
    ImGui::TextUnformatted(dog.message.c_str());
    char valueLine[128]{};
    std::snprintf(valueLine, sizeof(valueLine), "%.3f %s  (threshold %.3f)", dog.latest,
                  dog.unit.c_str(), dog.threshold);
    ImGui::TextUnformatted(valueLine);
    ImGui::PopTextWrapPos();
  } else if (dog.hideWhenOk) {
    draw->AddRectFilled(pos, {pos.x + width, pos.y + height},
                        PhotonUi::colorU32(PhotonUi::withAlpha(palette.raised, 0.45f)), 6.0f);
    ImGui::SetCursorScreenPos({pos.x + 10.0f, pos.y + 14.0f});
    ImGui::TextDisabled("OK  ·  %.3f %s", dog.latest, dog.unit.c_str());
  } else {
    draw->AddRectFilled(pos, {pos.x + width, pos.y + height},
                        PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.75f)), 6.0f);
    ImGui::SetCursorScreenPos({pos.x + 10.0f, pos.y + 14.0f});
    ImGui::Text("OK  ·  %.3f %s", dog.latest, dog.unit.c_str());
  }
  ImGui::SetCursorScreenPos(pos);
  ImGui::Dummy({width, height});
}

void CustomViewTab::resolveSources() {
  if (!arena) return;
  int unresolved = 0;
  int total = 0;
  std::string missingDetail;
  auto noteMissing = [&](const std::string& panelName, const std::string& widgetId,
                         uint32_t messageId, const std::string& signalName) {
    ++unresolved;
    char idBuf[32]{};
    std::snprintf(idBuf, sizeof(idBuf), "0x%03X", messageId);
    if (!missingDetail.empty()) missingDetail += '\n';
    missingDetail += panelName;
    missingDetail += " / ";
    missingDetail += widgetId;
    missingDetail += " · ";
    missingDetail += idBuf;
    if (!signalName.empty()) {
      missingDetail += " / ";
      missingDetail += signalName;
    }
  };

  for (auto& panel : panels) {
    for (auto& widget : panel.widgets) {
      if (widget.kind == CustomViewWidgetKind::CellGrid) {
        ++total;
        resolveCellGrid(widget.cellGrid);
        if (!widget.cellGrid.resolved)
          noteMissing(panel.name, widget.id, widget.cellGrid.voltageMessageId, "cell-grid");
        continue;
      }
      if (widget.kind == CustomViewWidgetKind::Watchdog) {
        ++total;
        resolveWatchdog(widget.watchdog);
        if (!widget.watchdog.source.assigned)
          noteMissing(panel.name, widget.id, widget.watchdog.source.messageId,
                      widget.watchdog.source.signalName);
        continue;
      }
      for (auto& source : widget.plot.sources) {
        ++total;
        source.assigned = false;
        Message* message =
            source.messageId < arena->messages.size() ? arena->messages[source.messageId] : nullptr;
        if (!message) {
          noteMissing(panel.name, widget.id, source.messageId, source.signalName);
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
          noteMissing(panel.name, widget.id, source.messageId, source.signalName);
          continue;
        }
        source.signalIndex = resolved;
        source.messageName = message->name;
        source.signalName = message->signals[resolved]->name;
        source.assigned = true;
      }
    }
  }
  resolvedGeneration = arena->generation;
  statusDetail = std::move(missingDetail);
  if (total == 0) return;
  if (unresolved > 0) {
    status = std::to_string(unresolved) + " of " + std::to_string(total) +
             " source(s) unresolved for active DBC.";
  } else {
    status = "All " + std::to_string(total) + " source(s) resolved against active DBC.";
    statusExpanded = false;
  }
}

void CustomViewTab::syncWithArena() {
  if (!arena) return;
  if (resolvedGeneration == arena->generation) return;
  resolveSources();
  plotManager().refreshForArena();
}

int CustomViewTab::findNextRow() const {
  int row = 0;
  for (const auto& widget : activeView().widgets)
    row = std::max(row, widget.rect.y + widget.rect.height);
  return row;
}

CustomViewRect CustomViewTab::clampRect(CustomViewRect rect) const {
  const CustomViewDefinition& view = activeView();
  rect.width = std::clamp(rect.width, 1, view.columns);
  rect.height = std::clamp(rect.height, 1, 48);
  rect.x = std::clamp(rect.x, 0, std::max(0, view.columns - rect.width));
  rect.y = std::max(0, rect.y);
  if (rect.x + rect.width > view.columns) rect.width = view.columns - rect.x;
  return rect;
}

CustomViewWidget* CustomViewTab::findWidget(const std::string& id) {
  for (auto& widget : activeView().widgets)
    if (widget.id == id) return &widget;
  return nullptr;
}

std::string CustomViewTab::buildDocumentJson() const {
  Json panelList = Json::array();
  for (const auto& panel : panels) panelList.push_back(panelToJson(panel));
  Json root = {{"$schema", "../docs/config/photon-view.schema.json"},
               {"schemaVersion", 1},
               {"id", "custom-views"},
               {"name", "Custom Views"},
               {"activePanel", activePanel},
               {"panels", std::move(panelList)}};
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

  CustomViewDefinition& view = activeView();
  int row = findNextRow();
  // ~320px tall on the fine 48px row grid (same visual weight as the old h=2 @ 160px).
  const int plotHeight = std::max(4, static_cast<int>(std::lround(320.0f / view.rowHeight)));
  for (auto& plot : taken) {
    CustomViewWidget widget{};
    widget.kind = CustomViewWidgetKind::Plot;
    widget.id = "plot-" + std::to_string(nextWidgetPlotId++);
    widget.rect = clampRect({0, row, view.columns, plotHeight});
    widget.plot = std::move(plot);
    widget.plot.open = true;
    row = widget.rect.y + widget.rect.height;
    view.widgets.push_back(std::move(widget));
  }
  if (view.id.empty()) view.id = "custom-view";
  if (view.name.empty()) view.name = "Panel";
  syncDocumentFromView("Added plot(s) to the canvas.", pathLoaded || path[0] != '\0');
}

void CustomViewTab::renderPreview() {
  CustomViewDefinition& view = activeView();
  if (view.widgets.empty()) {
    ImGui::TextDisabled(
        "No widgets in this panel. Add a plot, watchdog, or import a view.");
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
                            PhotonUi::colorU32(palette.text), widgetTitle(widget));
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
                              ImGuiWindowFlags_NoScrollbar)) {
          if (widget.kind == CustomViewWidgetKind::CellGrid)
            renderCellGrid(widget);
          else if (widget.kind == CustomViewWidgetKind::Watchdog)
            renderWatchdog(widget);
          else
            plotManager().renderEmbedded(widget.plot);
        }
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
    CustomViewDefinition& view = activeView();
    view.widgets.erase(std::remove_if(view.widgets.begin(), view.widgets.end(),
                                      [&](const CustomViewWidget& widget) {
                                        return widget.id == pendingDelete;
                                      }),
                       view.widgets.end());
    if (interactWidgetId == pendingDelete) {
      interactMode = CustomViewInteractMode::None;
      interactWidgetId.clear();
    }
    syncDocumentFromView("Removed widget from the canvas.", pathLoaded || path[0] != '\0');
  }
}

void CustomViewTab::renderStatus() {
  if (status.empty()) return;
  const PhotonUi::Palette palette = PhotonUi::palette();
  const bool isError = statusLooksLikeError(status);
  const bool hasDetail = !statusDetail.empty();
  const float width = ImGui::GetContentRegionAvail().x;
  const float buttonReserve = hasDetail ? 88.0f : 0.0f;
  const float textWidth = std::max(40.0f, width - 22.0f - buttonReserve);
  const float summaryHeight =
      std::max(ImGui::GetTextLineHeight(),
               ImGui::CalcTextSize(status.c_str(), nullptr, false, textWidth).y);
  float detailHeight = 0.0f;
  if (statusExpanded && hasDetail) {
    const float titleH = ImGui::GetTextLineHeight() + 4.0f;
    detailHeight = titleH +
                   ImGui::CalcTextSize(statusDetail.c_str(), nullptr, false, width - 22.0f).y +
                   8.0f;
  }
  const float height = std::max(34.0f, summaryHeight + detailHeight + 16.0f);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const ImVec2 max(pos.x + width, pos.y + height);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec4 fill =
      isError ? PhotonUi::withAlpha(palette.active, 0.42f) : PhotonUi::withAlpha(palette.panel, 0.76f);
  const ImVec4 border =
      isError ? PhotonUi::withAlpha(palette.accent, 0.55f) : PhotonUi::withAlpha(palette.border, 0.48f);
  draw->AddRectFilled(pos, max, PhotonUi::colorU32(fill), 8.0f);
  draw->AddRect(pos, max, PhotonUi::colorU32(border), 8.0f);

  ImGui::SetCursorScreenPos({pos.x + 10.0f, pos.y + 8.0f});
  if (isError) ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
  ImGui::PushTextWrapPos(pos.x + 10.0f + textWidth);
  ImGui::TextUnformatted(status.c_str());
  ImGui::PopTextWrapPos();
  if (isError) ImGui::PopStyleColor();

  if (hasDetail) {
    ImGui::SetCursorScreenPos(
        {pos.x + width - buttonReserve, pos.y + 6.0f});
    if (ImGui::SmallButton(statusExpanded ? "Hide##status" : "Details##status"))
      statusExpanded = !statusExpanded;
  }

  if (statusExpanded && hasDetail) {
    ImGui::SetCursorScreenPos({pos.x + 10.0f, pos.y + 8.0f + summaryHeight + 6.0f});
    ImGui::PushTextWrapPos(pos.x + width - 10.0f);
    if (isError) ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
    ImGui::TextUnformatted("Unresolved / missing:");
    if (isError) ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, palette.muted);
    ImGui::TextUnformatted(statusDetail.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
  }

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
  ensurePanels();
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
  syncWithArena();
  if (autoReload && !dirty && path[0] != '\0') {
    std::error_code error;
    const auto changedAt = std::filesystem::last_write_time(path.data(), error);
    if (!error && changedAt != loadedAt) load();
  }

  if (ImGui::Begin("Custom Views", nullptr, flags)) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false) && !io.WantTextInput && !dialogBusy)
      showFileDialog(CustomViewDialogAction::Open);

    renderPanelBar();
    ImGui::Separator();

    if (ImGui::Button("Add Plot")) {
      absorbBaseline = plotManager().windows.size();
      absorbArmed = true;
      plotManager().requestCreate();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Watchdog")) openWatchdogCreator();
    ImGui::SameLine();
    if (dialogBusy) ImGui::BeginDisabled();
    if (ImGui::Button(dialogBusy ? "Opening Explorer...##import" : "Import View...##import"))
      showFileDialog(CustomViewDialogAction::Open);
    if (dialogBusy) ImGui::EndDisabled();
    ImGui::SameLine();
    size_t widgetCount = 0;
    for (const auto& panel : panels) widgetCount += panel.widgets.size();
    const bool hasExportableView = widgetCount > 0 || panels.size() > 1;
    if (dialogBusy || !hasExportableView) ImGui::BeginDisabled();
    if (ImGui::Button(dialogBusy ? "Opening Explorer...##export" : "Export View...##export"))
      showFileDialog(CustomViewDialogAction::SaveAs);
    if (dialogBusy || !hasExportableView) ImGui::EndDisabled();
    if (activeView().widgets.empty()) {
      ImGui::TextDisabled(
          "This panel is empty. Add a plot, or switch panels with the tabs above.");
    }

    if (ImGui::BeginPopupContextWindow("##custom_view_menu")) {
      if (ImGui::MenuItem("Open View...", "Ctrl+O", false, !dialogBusy))
        showFileDialog(CustomViewDialogAction::Open);
      if (ImGui::MenuItem("Reload View", nullptr, false, path[0] != '\0')) load();
      if (ImGui::MenuItem("Export View...", nullptr, false, hasExportableView && !dialogBusy))
        showFileDialog(CustomViewDialogAction::SaveAs);
      if (ImGui::MenuItem("Add Panel")) addPanel();
      if (ImGui::MenuItem("Remove Panel", nullptr, false, panels.size() > 1))
        removePanel(activePanel);
      if (ImGui::MenuItem("Export Signal Catalog")) exportSignalCatalog();
      ImGui::EndPopup();
    }

    renderStatus();
    ImGui::Separator();
    renderPreview();
  }
  ImGui::End();
  plotManager().drawCreator();
  renderWatchdogCreator();
  absorbCreatedPlots();
}

CustomViewTab& customViewTab() {
  static CustomViewTab tab{};
  return tab;
}
