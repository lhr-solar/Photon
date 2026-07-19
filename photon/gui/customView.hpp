#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "../parse/arena.hpp"
#include "customViewTypes.hpp"
#include "imgui.h"
#include "plots.hpp"

struct SDL_Window;

enum class CustomViewDialogAction { None, Open, SaveAs };

enum class CustomViewInteractMode { None, Drag, Resize };

struct CustomViewTab {
  Arena* arena = nullptr;
  SDL_Window* window = nullptr;
  uint64_t resolvedGeneration = 0;
  std::array<char, 512> path{"views/custom.photon-view.json"};
  std::array<char, 65536> document{};
  std::vector<CustomViewDefinition> panels{};
  int activePanel = 0;
  bool forceSelectPanel = false;
  std::string status{"Create a new view or open a saved config."};
  std::string statusDetail{};
  bool statusExpanded = false;
  std::filesystem::file_time_type loadedAt{};
  bool autoReload = true;
  bool dirty = false;
  bool pathLoaded = false;
  bool absorbArmed = false;
  size_t absorbBaseline = 0;
  int nextWidgetPlotId = 10000;
  int nextPanelId = 1;
  bool watchdogCreatorOpen = false;
  bool watchdogCreatorFocusSearch = false;
  CustomViewWatchdog pendingWatchdog{};
  std::array<char, 128> watchdogSearch{};
  std::mutex dialogMutex{};
  std::string pendingDialogPath{};
  std::string pendingDialogError{};
  CustomViewDialogAction activeDialogAction = CustomViewDialogAction::None;
  CustomViewDialogAction pendingDialogAction = CustomViewDialogAction::None;
  CustomViewDialogAction requestedDialogAction = CustomViewDialogAction::None;
  bool dialogActive = false;

  CustomViewInteractMode interactMode = CustomViewInteractMode::None;
  std::string interactWidgetId{};
  CustomViewRect interactStartRect{};
  ImVec2 interactStartMouse{};
  CustomViewRect interactPreviewRect{};

  void init(Arena* arenaTarget, SDL_Window* windowTarget);
  void draw(ImGuiWindowFlags flags);
  // Re-bind plot/watchdog sources when the active DBC (arena generation) changes.
  // Safe to call every frame from GUI; no-ops when generation is unchanged.
  void syncWithArena();

  CustomViewDefinition& activeView();
  const CustomViewDefinition& activeView() const;

 private:
  void ensurePanels();
  void addPanel();
  void removePanel(int index);
  void setActivePanel(int index);
  void renderPanelBar();

  void newDocument();
  bool load();
  bool save();
  bool parseDocument();
  void resolveSources();
  void resolveCellGrid(CustomViewCellGrid& grid);
  void renderCellGrid(CustomViewWidget& widget);
  void resolveWatchdog(CustomViewWatchdog& watchdog);
  void updateWatchdog(CustomViewWatchdog& watchdog);
  void renderWatchdog(CustomViewWidget& widget);
  void addCellGrid();
  void addCanMonitor();
  void openWatchdogCreator();
  void renderWatchdogCreator();
  void commitWatchdogCreator();
  void renderPreview();
  void renderStatus();
  void exportSignalCatalog();
  bool exportCurrentView(const std::filesystem::path& outputPath);
  void showFileDialog(CustomViewDialogAction action);
  void pumpFileDialogRequest();
  void consumeFileDialog();

  int findNextRow() const;
  void absorbCreatedPlots();
  std::string buildDocumentJson() const;
  void syncDocumentFromView(std::string_view statusMessage, bool writeToDisk);
  void commitLayoutEdit();
  CustomViewRect clampRect(CustomViewRect rect) const;
  CustomViewWidget* findWidget(const std::string& id);
};

CustomViewTab& customViewTab();
