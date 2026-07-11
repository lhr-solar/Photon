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

struct CustomViewRect {
  int x = 0;
  int y = 0;
  int width = 6;
  int height = 2;
};

struct CustomViewWidget {
  std::string id{};
  CustomViewRect rect{};
  PlotManager::PlotWindow plot{};
};

struct CustomViewDefinition {
  int schemaVersion = 1;
  std::string id = "custom-view";
  std::string name = "Custom View";
  int columns = 12;
  float rowHeight = 160.0f;
  float gap = 12.0f;
  std::vector<CustomViewWidget> widgets{};
};

struct CustomViewTab {
  Arena* arena = nullptr;
  SDL_Window* window = nullptr;
  uint64_t resolvedGeneration = 0;
  std::array<char, 512> path{"views/custom.photon-view.json"};
  std::array<char, 65536> document{};
  CustomViewDefinition view{};
  std::string status{"Create a new view or open an agent-generated config."};
  std::filesystem::file_time_type loadedAt{};
  bool autoReload = true;
  bool dirty = false;
  bool pathLoaded = false;
  bool absorbArmed = false;
  size_t absorbBaseline = 0;
  int nextWidgetPlotId = 10000;
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

 private:
  void newDocument();
  bool load();
  bool save();
  bool parseDocument();
  void resolveSources();
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
