#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../parse/arena.hpp"
#include "imgui.h"
#include "implot.h"

struct Plots {
  static bool signal(Arena& arena, uint32_t id, uint32_t signal, ImVec2 size,
                     const ImPlotSpec& spec = ImPlotSpec());
};

struct PlotManager {
  static constexpr double kDefaultTimeWindowSeconds = 60.0;
  static constexpr double kMinTimeWindowSeconds = 0.1;
  static constexpr double kMaxTimeWindowSeconds = 24.0 * 60.0 * 60.0;

  struct PlotSourceRef {
    uint32_t messageId = 0;
    uint32_t signalIndex = 0;
    std::string messageName{};
    std::string signalName{};
    bool assigned = false;
  };

  struct SignalOption {
    PlotSourceRef ref{};
    std::string label{};
  };

  struct PlotTypeSpec {
    const char* label = "";
    int minSources = 1;
    int maxSources = 1;
    bool usesTimeAxis = true;
    bool is3D = false;
  };

  struct PlotWindow {
    int id = 0;
    int typeIndex = 0;
    std::vector<PlotSourceRef> sources{};
    std::string title{};
    bool open = true;
    bool useSource1TimeAsX = true;
    bool followLatest = true;
    bool hasView = false;
    double timeWindowSeconds = kDefaultTimeWindowSeconds;
    double xMin = 0.0;
    double xMax = 0.0;
  };

  Arena* arena = nullptr;
  uint64_t arenaGeneration = 0;
  ImGuiID homeDockspaceID = 0;
  bool creatorOpen = false;
  bool creatorFocusSearch = false;
  int typeIndex = 0;
  std::vector<PlotSourceRef> pendingSources{};
  char search[128]{};
  std::vector<SignalOption> signalOptions{};
  std::vector<int> sourceMatches{};
  int selectedMatch = -1;
  int activeSourceIndex = 0;
  bool useSource1TimeAsX = true;
  int nextPlotId = 1;
  std::vector<PlotWindow> windows{};

  void init(Arena* arenaTarget);
  void draw(ImGuiWindowFlags flags);
  void requestCreate();
  void drawCreatedPlots();
  void drawCreator();
  void renderEmbedded(PlotWindow& plot);
  const std::vector<PlotWindow>& createdPlots() const { return windows; }
  std::vector<PlotWindow> takeWindows();
  void clearWindows() { windows.clear(); }
  static int typeFromKey(std::string_view key);
  static const char* typeKey(int typeIndex);
  static const PlotTypeSpec& typeSpec(int typeIndex);
  void handleHotkeys(bool homeActive);
  void renderHome(ImGuiID dockspaceID, const ImVec2& contentMin, const ImVec2& contentMax);
  bool hasPlots() const { return !windows.empty(); }
  void refreshForArena();

 private:
  static const PlotTypeSpec& specFor(int typeIndex);
  void openCreator();
  void refreshSignalOptions();
  void refreshMatches();
  void resetPendingSourcesForType();
  void renderCreator();
  void renderPlotWindows();
  void createPlot();
};

PlotManager& plotManager();
