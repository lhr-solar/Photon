#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../parse/arena.hpp"
#include "imgui.h"
#include "implot.h"

struct Network;

enum class TimelineMode : uint8_t { Paused, Buffering, Playing, Live, Unavailable };

struct Plots {
  static bool signalStatic(Arena& arena, uint32_t id, uint32_t signal, ImVec2 size,
                           const ImPlotSpec& spec = ImPlotSpec());
  bool signal(Arena& arena, uint32_t id, uint32_t signal, ImVec2 size,
              const ImPlotSpec& spec = ImPlotSpec());
  void timeline(Arena& arena, Network* network, bool serverConnected, ImVec2 pos, ImVec2 size);
  double cursor{};
  // Live View / Dynamics should not wait for the bottom timeline widget to run later in
  // the frame. In Live mode this returns wall-clock now; otherwise the scrubber cursor.
  double mapCursor() const;

 private:
  struct CursorIndex {
    uint64_t generation = -1;
    double time{};
    double window{};
    uint32_t id = MESSAGE_MAX;
    uint32_t count{};
    uint32_t first{};
    uint32_t last{};
  } index;
  TimelineMode timelineMode = TimelineMode::Live;
  double playTarget{};
  double bufferingSince{};
  uint64_t playbackStatusSequence{};
  double windowSeconds = 2.0;
  int calendarYear{};
  int calendarMonth{};
  uint8_t timelineLevel{};
  uint8_t timelineDragging{};
};

enum PlotTypeIndex : int {
  PlotType_Line = 0,
  PlotType_FilledLine,
  PlotType_Shaded,
  PlotType_Scatter,
  PlotType_Stairstep,
  PlotType_Bar,
  PlotType_BarGroups,
  PlotType_BarStacks,
  PlotType_ErrorBars,
  PlotType_Stem,
  PlotType_Pie,
  PlotType_Heatmap,
  PlotType_Histogram,
  PlotType_Histogram2D,
  PlotType_Digital,
  PlotType_3DLine,
  PlotType_3DScatter,
  PlotType_3DSurface,
  PlotType_List,
  PlotType_Count
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
    bool operator==(const PlotSourceRef&) const = default;
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
  Network* network = nullptr;
  uint64_t arenaGeneration = 0;
  ImGuiID homeDockspaceID = 0;
  bool creatorOpen = false;
  bool creatorFocusSearch = false;
  int editingPlotId = 0;
  PlotWindow* editTarget = nullptr;
  bool editApplied = false;
  int typeIndex = 0;
  std::vector<PlotSourceRef> pendingSources{};
  char search[128]{};
  char pendingTitle[128]{};
  std::vector<SignalOption> signalOptions{};
  std::vector<int> sourceMatches{};
  int selectedMatch = -1;
  int activeSourceIndex = 0;
  bool useSource1TimeAsX = true;
  int nextPlotId = 1;
  std::vector<PlotWindow> windows{};

  void init(Arena* arenaTarget, Network* networkTarget = nullptr);
  void draw(ImGuiWindowFlags flags);
  void requestCreate();
  void requestEdit(PlotWindow& plot);
  bool consumeEditApplied();
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
  void openEditor(int plotId);
  void refreshSignalOptions();
  void refreshMatches();
  void resetPendingSourcesForType();
  void renderCreator();
  void renderPlotWindows();
  void createPlot();
  void applyPlotEdits();
};

PlotManager& plotManager();
