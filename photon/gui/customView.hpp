#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "../parse/arena.hpp"
#include "imgui.h"
#include "plots.hpp"

struct SDL_Window;

enum class CustomViewDialogAction { None, Open, SaveAs };

enum class CustomViewInteractMode { None, Drag, Resize };

enum class CustomViewWidgetKind { Plot, CellGrid, Watchdog };

enum class CustomViewCellGridMode { Voltage, Temperature };

enum class CustomViewWatchdogCompare { Below, Above };

constexpr int kCellGridCapacity = 32;

struct CustomViewRect {
  int x = 0;
  int y = 0;
  int width = 6;
  int height = 2;
};

struct CustomViewCellSample {
  double voltage = 0.0;
  double temperature = 0.0;
  double voltageFault = 0.0;
  double temperatureFault = 0.0;
  double voltageAgeMs = 0.0;
  double temperatureAgeMs = 0.0;
  bool hasVoltage = false;
  bool hasTemperature = false;
};

struct CustomViewCellGrid {
  std::string title = "Battery Cells";
  int cols = 8;
  int rows = 4;
  CustomViewCellGridMode mode = CustomViewCellGridMode::Voltage;
  uint32_t voltageMessageId = 11;
  uint32_t temperatureMessageId = 12;
  uint32_t statusMessageId = 1;

  uint32_t voltageTapIdx = SIGNAL_MAX;
  uint32_t voltageDataIdx = SIGNAL_MAX;
  uint32_t voltageFaultIdx = SIGNAL_MAX;
  uint32_t voltageAgeIdx = SIGNAL_MAX;
  uint32_t temperatureTapIdx = SIGNAL_MAX;
  uint32_t temperatureDataIdx = SIGNAL_MAX;
  uint32_t temperatureFaultIdx = SIGNAL_MAX;
  uint32_t temperatureAgeIdx = SIGNAL_MAX;
  uint32_t statusPackVoltageIdx = SIGNAL_MAX;
  uint32_t statusAvgTempIdx = SIGNAL_MAX;
  uint32_t statusFaultIdx = SIGNAL_MAX;
  bool resolved = false;

  std::array<CustomViewCellSample, kCellGridCapacity> cells{};
  double packVoltage = 0.0;
  double packAvgTemp = 0.0;
  double packFault = 0.0;
  bool hasPackVoltage = false;
  bool hasPackAvgTemp = false;
  bool hasPackFault = false;
};

struct CustomViewWatchdog {
  std::string title = "Watchdog";
  std::string message = "Signal out of range";
  std::string unit{};
  CustomViewWatchdogCompare comparison = CustomViewWatchdogCompare::Below;
  double threshold = 0.0;
  bool hideWhenOk = true;
  PlotManager::PlotSourceRef source{};
  double latest = 0.0;
  bool hasValue = false;
  bool tripped = false;
};

struct CustomViewWidget {
  std::string id{};
  CustomViewWidgetKind kind = CustomViewWidgetKind::Plot;
  CustomViewRect rect{};
  PlotManager::PlotWindow plot{};
  CustomViewCellGrid cellGrid{};
  CustomViewWatchdog watchdog{};
};

struct CustomViewDefinition {
  int schemaVersion = 1;
  std::string id = "custom-view";
  std::string name = "Panel";
  int columns = 48;
  float rowHeight = 48.0f;
  float gap = 8.0f;
  std::vector<CustomViewWidget> widgets{};
};

struct CustomViewTab {
  Arena* arena = nullptr;
  SDL_Window* window = nullptr;
  uint64_t resolvedGeneration = 0;
  std::array<char, 512> path{"views/custom.photon-view.json"};
  std::array<char, 65536> document{};
  std::vector<CustomViewDefinition> panels{};
  int activePanel = 0;
  bool forceSelectPanel = false;
  std::string status{"Create a new view or open an agent-generated config."};
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
  void updateCellGridSnapshot(CustomViewCellGrid& grid);
  void renderCellGrid(CustomViewWidget& widget);
  void resolveWatchdog(CustomViewWatchdog& watchdog);
  void updateWatchdog(CustomViewWatchdog& watchdog);
  void renderWatchdog(CustomViewWidget& widget);
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
