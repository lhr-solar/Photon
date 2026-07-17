#include "customView.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>

#include "customViewCellGrid.hpp"
#include "customViewCan.hpp"
#include "customViewDocument.hpp"
#include "customViewTelemetry.hpp"
#include "customViewWatchdog.hpp"
#include "json.hpp"
#include "uiComponents.hpp"

namespace {
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

const char* widgetTitle(const CustomViewWidget& widget) {
  if (widget.kind == CustomViewWidgetKind::CellGrid) return widget.cellGrid.title.c_str();
  if (widget.kind == CustomViewWidgetKind::Watchdog) return widget.watchdog.title.c_str();
  if (widget.kind == CustomViewWidgetKind::CanMonitor) return widget.canMonitor.title.c_str();
  return widget.plot.title.c_str();
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
    CustomViewDocumentState state = CustomViewDocument::parse(document.data());
    panels = std::move(state.panels);
    activePanel = state.activePanel;
    forceSelectPanel = true;
    nextWidgetPlotId = state.nextWidgetPlotId;
    nextPanelId = state.nextPanelId;
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
  if (!arena) return;
  CustomViewTelemetry::resolve(*arena, grid);
}

void CustomViewTab::renderCellGrid(CustomViewWidget& widget) {
  CustomViewCellGridWidget::draw(arena, widget);
}

void CustomViewTab::resolveWatchdog(CustomViewWatchdog& watchdog) {
  if (!arena) return;
  CustomViewTelemetry::resolve(*arena, watchdog);
}

void CustomViewTab::updateWatchdog(CustomViewWatchdog& watchdog) {
  if (!arena) return;
  CustomViewTelemetry::update(*arena, watchdog);
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
      pendingWatchdog.comparison =
          compareIndex == 1 ? CustomViewWatchdogCompare::Above : CustomViewWatchdogCompare::Below;
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
          for (char& c : needle) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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
            pendingWatchdog.title =
                option.ref.signalName.empty() ? option.ref.messageName : option.ref.signalName;
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
  CustomViewWatchdogWidget::draw(arena, widget);
}

void CustomViewTab::addCanMonitor() {
  CustomViewDefinition& view = activeView();
  CustomViewWidget widget{};
  widget.kind = CustomViewWidgetKind::CanMonitor;
  widget.id = "can-monitor-" + std::to_string(nextWidgetPlotId++);
  widget.rect = clampRect({0, findNextRow(), view.columns, 9});
  view.widgets.push_back(std::move(widget));
  syncDocumentFromView("Added CAN monitor widget.", pathLoaded || path[0] != '\0');
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
        CustomViewTelemetry::resolve(*arena, source);
        if (!source.assigned) {
          noteMissing(panel.name, widget.id, source.messageId, source.signalName);
        }
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
  return CustomViewDocument::serialize(panels, activePanel);
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
    ImGui::TextDisabled("No widgets in this panel. Add a plot, watchdog, or import a view.");
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
        widgetDraw->AddRectFilled(
            titleMin, barMax, PhotonUi::colorU32(PhotonUi::withAlpha(palette.raised, 0.95f)), 4.0f);
        widgetDraw->AddText(
            {titleMin.x + 8.0f, titleMin.y + (kTitleBarHeight - ImGui::GetTextLineHeight()) * 0.5f},
            PhotonUi::colorU32(palette.muted), "::");
        widgetDraw->AddText({titleMin.x + 28.0f,
                             titleMin.y + (kTitleBarHeight - ImGui::GetTextLineHeight()) * 0.5f},
                            PhotonUi::colorU32(palette.text), widgetTitle(widget));
        if (titleHovered || titleActive) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

        ImGui::SameLine(0.0f, 0.0f);
        if (widget.kind == CustomViewWidgetKind::Plot && ImGui::SmallButton("Edit##edit"))
          plotManager().requestEdit(widget.plot);
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
          else if (widget.kind == CustomViewWidgetKind::CanMonitor)
            CustomViewCan::drawMonitor(arena, plotManager().network, widget);
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
        widgetDraw->AddRectFilled(
            gripMin, gripMax, PhotonUi::colorU32(PhotonUi::withAlpha(palette.raised, 0.9f)), 3.0f);
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
          next.width = interactStartRect.width + static_cast<int>(std::lround(dx / colStride));
          next.height = interactStartRect.height + static_cast<int>(std::lround(dy / rowStride));
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
    view.widgets.erase(
        std::remove_if(view.widgets.begin(), view.widgets.end(),
                       [&](const CustomViewWidget& widget) { return widget.id == pendingDelete; }),
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
  const float summaryHeight = std::max(
      ImGui::GetTextLineHeight(), ImGui::CalcTextSize(status.c_str(), nullptr, false, textWidth).y);
  float detailHeight = 0.0f;
  if (statusExpanded && hasDetail) {
    const float titleH = ImGui::GetTextLineHeight() + 4.0f;
    detailHeight =
        titleH + ImGui::CalcTextSize(statusDetail.c_str(), nullptr, false, width - 22.0f).y + 8.0f;
  }
  const float height = std::max(34.0f, summaryHeight + detailHeight + 16.0f);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const ImVec2 max(pos.x + width, pos.y + height);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec4 fill = isError ? PhotonUi::withAlpha(palette.active, 0.42f)
                              : PhotonUi::withAlpha(palette.panel, 0.76f);
  const ImVec4 border = isError ? PhotonUi::withAlpha(palette.accent, 0.55f)
                                : PhotonUi::withAlpha(palette.border, 0.48f);
  draw->AddRectFilled(pos, max, PhotonUi::colorU32(fill), 8.0f);
  draw->AddRect(pos, max, PhotonUi::colorU32(border), 8.0f);

  ImGui::SetCursorScreenPos({pos.x + 10.0f, pos.y + 8.0f});
  if (isError) ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
  ImGui::PushTextWrapPos(pos.x + 10.0f + textWidth);
  ImGui::TextUnformatted(status.c_str());
  ImGui::PopTextWrapPos();
  if (isError) ImGui::PopStyleColor();

  if (hasDetail) {
    ImGui::SetCursorScreenPos({pos.x + width - buttonReserve, pos.y + 6.0f});
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
  using Json = nlohmann::json;
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
    status = "Exported signal context to " + outputPath.string();
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

    if (ImGui::Button("Add Plot / Signals")) {
      absorbBaseline = plotManager().windows.size();
      absorbArmed = true;
      plotManager().requestCreate();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Watchdog")) openWatchdogCreator();
    ImGui::SameLine();
    if (ImGui::Button("Add CAN Monitor")) addCanMonitor();
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
      ImGui::TextDisabled("This panel is empty. Add a plot, or switch panels with the tabs above.");
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
  if (plotManager().consumeEditApplied())
    syncDocumentFromView("Updated plot component.", pathLoaded || path[0] != '\0');
  renderWatchdogCreator();
  absorbCreatedPlots();
}

CustomViewTab& customViewTab() {
  static CustomViewTab tab{};
  return tab;
}
