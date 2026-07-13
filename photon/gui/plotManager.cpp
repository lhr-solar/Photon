#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "plotRenderer.hpp"
#include "plots.hpp"

namespace {
constexpr std::array<PlotManager::PlotTypeSpec, PlotType_Count> kPlotSpecs{{
    {"Line Plots", 1, 8, true, false},       {"Filled Line Plots", 1, 8, true, false},
    {"Shaded Plots", 2, 2, true, false},     {"Scatter Plots", 1, 8, true, false},
    {"Stairstep Plots", 1, 8, true, false},  {"Bar Plots", 1, 8, true, false},
    {"Bar Groups", 2, 8, true, false},       {"Bar Stacks", 2, 8, true, false},
    {"Error Bars", 2, 2, true, false},       {"Stem Plots", 1, 8, true, false},
    {"Pie Charts", 1, 8, false, false},      {"Heatmaps", 1, 1, false, false},
    {"Histogram", 1, 8, false, false},       {"Histogram 2D", 2, 2, false, false},
    {"Digital Plots", 1, 8, true, false},    {"3D Line Plots", 2, 3, false, true},
    {"3D Scatter Plots", 2, 3, false, true}, {"3D Surface Plots", 2, 3, false, true},
    {"List", 1, 128, false, false},
}};

constexpr std::array<const char*, PlotType_Count> kPlotTypeKeys{{
    "line",    "filled-line", "shaded",     "scatter",      "stairstep",
    "bar",     "bar-groups",  "bar-stacks", "error-bars",   "stem",
    "pie",     "heatmap",     "histogram",  "histogram-2d", "digital",
    "line-3d", "scatter-3d",  "surface-3d", "list",
}};

int required3DSources(bool useSource1TimeAsX) { return useSource1TimeAsX ? 2 : 3; }

const char* threeDSourceLabel(size_t sourceIndex, bool useSource1TimeAsX) {
  if (useSource1TimeAsX) return sourceIndex == 0 ? "Y Source" : "Z Source";
  if (sourceIndex == 0) return "X Source";
  if (sourceIndex == 1) return "Y Source";
  return "Z Source";
}

std::string sourceSlotLabel(int typeIndex, size_t sourceIndex, bool useSource1TimeAsX) {
  if (kPlotSpecs[static_cast<size_t>(typeIndex)].is3D)
    return threeDSourceLabel(sourceIndex, useSource1TimeAsX);
  char label[32]{};
  std::snprintf(label, sizeof(label), "Signal %zu", sourceIndex + 1);
  return label;
}

Message* findMessage(Arena* arena, uint32_t messageId) {
  if (!arena || messageId >= arena->messages.size()) return nullptr;
  return arena->messages[messageId];
}

Signal* findSignal(Arena* arena, const PlotManager::PlotSourceRef& ref) {
  if (!ref.assigned) return nullptr;
  Message* msg = findMessage(arena, ref.messageId);
  if (!msg) return nullptr;
  if (!ref.signalName.empty()) {
    for (uint32_t index = 0; index < msg->signalCount; ++index) {
      Signal* signal = msg->signals[index];
      if (signal && signal->name == ref.signalName) return signal;
    }
    return nullptr;
  }
  if (ref.signalIndex >= msg->signalCount) return nullptr;
  return msg->signals[ref.signalIndex];
}

bool hasAllSources(const std::vector<PlotManager::PlotSourceRef>& sources) {
  if (sources.empty()) return false;
  for (const auto& source : sources)
    if (!source.assigned) return false;
  return true;
}

std::string signalDisplayName(Arena* arena, const PlotManager::PlotSourceRef& ref) {
  Message* msg = findMessage(arena, ref.messageId);
  Signal* sig = findSignal(arena, ref);
  if (!msg || !sig) {
    if (!ref.messageName.empty() && !ref.signalName.empty())
      return ref.messageName + " / " + ref.signalName;
    if (!ref.signalName.empty()) return ref.signalName;
    return "<unassigned>";
  }
  if (!msg->name.empty()) return msg->name + " / " + sig->name;
  char label[256]{};
  std::snprintf(label, sizeof(label), "0x%03X / %s", msg->id, sig->name.c_str());
  return label;
}

std::string makePlotTitle(Arena* arena, int typeIndex,
                          const std::vector<PlotManager::PlotSourceRef>& sources) {
  std::string sourcesPart{};
  for (size_t i = 0; i < sources.size(); ++i) {
    if (i > 0) sourcesPart += ", ";
    sourcesPart += signalDisplayName(arena, sources[i]);
  }
  if (sourcesPart.empty()) sourcesPart = "Untitled";
  return sourcesPart + " · " + kPlotSpecs[static_cast<size_t>(typeIndex)].label;
}

bool matchQuery(const std::string& haystack, const char* query) {
  if (!query || query[0] == '\0') return true;
  std::string lowerHaystack = haystack;
  std::string lowerQuery = query;
  std::transform(lowerHaystack.begin(), lowerHaystack.end(), lowerHaystack.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lowerHaystack.find(lowerQuery) != std::string::npos;
}
}  // namespace

const PlotManager::PlotTypeSpec& PlotManager::specFor(int index) {
  return kPlotSpecs[static_cast<size_t>(std::clamp(index, 0, PlotType_Count - 1))];
}

const PlotManager::PlotTypeSpec& PlotManager::typeSpec(int index) { return specFor(index); }

const char* PlotManager::typeKey(int index) {
  return kPlotTypeKeys[static_cast<size_t>(std::clamp(index, 0, PlotType_Count - 1))];
}

int PlotManager::typeFromKey(std::string_view key) {
  for (size_t index = 0; index < kPlotTypeKeys.size(); ++index)
    if (key == kPlotTypeKeys[index] || key == kPlotSpecs[index].label)
      return static_cast<int>(index);
  return -1;
}

void PlotManager::init(Arena* arenaTarget) {
  if (arena == arenaTarget) return;
  arena = arenaTarget;
  arenaGeneration = arena ? arena->generation : 0;
  typeIndex = PlotType_Line;
  useSource1TimeAsX = true;
  resetPendingSourcesForType();
  refreshSignalOptions();
  refreshMatches();
}

void PlotManager::refreshForArena() {
  if (!arena || arenaGeneration == arena->generation) return;
  arenaGeneration = arena->generation;
  auto refreshRef = [this](PlotSourceRef& ref) {
    Message* message = findMessage(arena, ref.messageId);
    if (!message) {
      ref.assigned = false;
      return;
    }
    uint32_t index = SIGNAL_MAX;
    if (!ref.signalName.empty()) {
      for (uint32_t candidate = 0; candidate < message->signalCount; ++candidate) {
        Signal* signal = message->signals[candidate];
        if (signal && signal->name == ref.signalName) {
          index = candidate;
          break;
        }
      }
    }
    if (index == SIGNAL_MAX && ref.signalName.empty() && ref.signalIndex < message->signalCount &&
        message->signals[ref.signalIndex])
      index = ref.signalIndex;
    if (index == SIGNAL_MAX) {
      ref.assigned = false;
      return;
    }
    ref.signalIndex = index;
    ref.messageName = message->name;
    ref.signalName = message->signals[index]->name;
    ref.assigned = true;
  };
  for (auto& source : pendingSources) refreshRef(source);
  for (auto& window : windows)
    for (auto& source : window.sources) refreshRef(source);
  refreshSignalOptions();
  refreshMatches();
}

void PlotManager::handleHotkeys(bool homeActive) {
  if (!homeActive) return;
  ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsKeyPressed(ImGuiKey_Slash, false) && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper &&
      !io.WantTextInput) {
    openCreator();
  }
}

void PlotManager::draw(ImGuiWindowFlags flags) {
  refreshForArena();
  handleHotkeys(true);
  if (ImGui::Begin("Plots", nullptr, flags)) {
    if (ImGui::Button("Create Plot")) openCreator();
    if (!windows.empty()) {
      ImGui::SameLine();
      if (ImGui::Button("Clear All")) windows.clear();
    }
    ImGui::Separator();
    if (windows.empty()) {
      ImGui::Dummy(ImVec2(0.0f, 24.0f));
      ImGui::TextDisabled("Create a plot, then assign exact message IDs and signals.");
      ImGui::TextDisabled("Press / anywhere on this tab to open the plot creator.");
    } else {
      renderPlotWindows();
    }
  }
  ImGui::End();
  renderCreator();
}

void PlotManager::requestCreate() { openCreator(); }

void PlotManager::drawCreatedPlots() {
  refreshForArena();
  if (!windows.empty()) renderPlotWindows();
}

void PlotManager::drawCreator() {
  refreshForArena();
  renderCreator();
}

std::vector<PlotManager::PlotWindow> PlotManager::takeWindows() {
  std::vector<PlotWindow> taken = std::move(windows);
  windows.clear();
  return taken;
}

void PlotManager::renderEmbedded(PlotWindow& plot) { PlotRenderer::render(arena, plot); }

void PlotManager::renderHome(ImGuiID dockspaceID, const ImVec2& contentMin,
                             const ImVec2& contentMax) {
  homeDockspaceID = dockspaceID;
  refreshForArena();
  renderCreator();
  renderPlotWindows();
  if (windows.empty() && !creatorOpen) {
    const char* hint = "Press \"/\" to create plots";
    const ImVec2 textSize = ImGui::CalcTextSize(hint);
    const float xCenter = (contentMin.x + contentMax.x) * 0.5f;
    const float yTopThird = contentMin.y + ((contentMax.y - contentMin.y) * 0.28f);
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    drawList->AddText(ImVec2(xCenter - textSize.x * 0.5f, yTopThird - textSize.y * 0.5f),
                      ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.92f)), hint);
  }
}

void PlotManager::openCreator() {
  refreshForArena();
  creatorOpen = true;
  creatorFocusSearch = true;
  typeIndex = PlotType_Line;
  useSource1TimeAsX = true;
  search[0] = '\0';
  activeSourceIndex = 0;
  selectedMatch = -1;
  resetPendingSourcesForType();
  refreshMatches();
}

void PlotManager::refreshSignalOptions() {
  signalOptions.clear();
  if (!arena) return;
  for (uint32_t messageId : arena->validIds) {
    Message* msg = findMessage(arena, messageId);
    if (!msg) continue;
    for (uint32_t signalIndex = 0; signalIndex < msg->signalCount; ++signalIndex) {
      Signal* sig = msg->signals[signalIndex];
      if (!sig) continue;
      SignalOption option{};
      option.ref.messageId = messageId;
      option.ref.signalIndex = signalIndex;
      option.ref.messageName = msg->name;
      option.ref.signalName = sig->name;
      option.ref.assigned = true;
      char label[256]{};
      std::snprintf(label, sizeof(label), "0x%03X (%u) : %s / %s", messageId, messageId,
                    msg->name.c_str(), sig->name.c_str());
      option.label = label;
      signalOptions.push_back(std::move(option));
    }
  }
}

void PlotManager::refreshMatches() {
  sourceMatches.clear();
  for (size_t i = 0; i < signalOptions.size(); ++i) {
    if (matchQuery(signalOptions[i].label, search)) sourceMatches.push_back(static_cast<int>(i));
  }
  if (sourceMatches.empty())
    selectedMatch = -1;
  else
    selectedMatch = std::clamp(selectedMatch, 0, static_cast<int>(sourceMatches.size()) - 1);
}

void PlotManager::resetPendingSourcesForType() {
  const PlotTypeSpec& spec = specFor(typeIndex);
  const int count = spec.is3D ? required3DSources(useSource1TimeAsX) : spec.minSources;
  pendingSources.assign(static_cast<size_t>(count), PlotSourceRef{});
  activeSourceIndex = std::clamp(activeSourceIndex, 0, std::max(0, count - 1));
}

void PlotManager::renderCreator() {
  if (!creatorOpen) return;

  const ImGuiStyle& style = ImGui::GetStyle();
  float maxSuggestionWidth = 0.0f;
  for (int optionIndex : sourceMatches) {
    if (optionIndex < 0 || static_cast<size_t>(optionIndex) >= signalOptions.size()) continue;
    maxSuggestionWidth = std::max(
        maxSuggestionWidth,
        ImGui::CalcTextSize(signalOptions[static_cast<size_t>(optionIndex)].label.c_str()).x);
  }
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float previewColumnWidth = 430.0f;
  const float assignedColumnWidth = 300.0f;
  const float minSearchColumnWidth = 320.0f;
  const float desiredSearchColumnWidth =
      std::max(minSearchColumnWidth, maxSuggestionWidth + style.FramePadding.x * 2.0f +
                                         style.CellPadding.x * 2.0f + style.ScrollbarSize + 24.0f);
  const float availableViewportWidth =
      viewport ? viewport->WorkSize.x : ImGui::GetIO().DisplaySize.x;
  const float maxWindowWidth = std::max(720.0f, availableViewportWidth - 48.0f);
  const float desiredWindowWidth =
      std::min(maxWindowWidth, previewColumnWidth + assignedColumnWidth + desiredSearchColumnWidth +
                                   style.WindowPadding.x * 2.0f + style.CellPadding.x * 6.0f +
                                   style.ItemSpacing.x * 2.0f);
  const ImVec2 center = viewport ? viewport->GetCenter() : ImVec2(0.0f, 0.0f);
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(desiredWindowWidth, 0.0f), ImGuiCond_Always);
  bool keepOpen = true;
  const ImGuiStyle& theme = ImGui::GetStyle();
  ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.Colors[ImGuiCol_PopupBg]);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.Colors[ImGuiCol_FrameBg]);
  ImGui::PushStyleColor(ImGuiCol_Border, theme.Colors[ImGuiCol_Border]);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  if (!ImGui::Begin("Create Plot", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking)) {
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    creatorOpen = keepOpen;
    return;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) keepOpen = false;
  const PlotTypeSpec& spec = specFor(typeIndex);

  if (ImGui::BeginTable("##plot_creator", 3, ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, previewColumnWidth);
    ImGui::TableSetupColumn("Assigned", ImGuiTableColumnFlags_WidthFixed, assignedColumnWidth);
    ImGui::TableSetupColumn("Search", ImGuiTableColumnFlags_WidthFixed, desiredSearchColumnWidth);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Plot Type");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##plot_type", spec.label)) {
      for (int i = 0; i < PlotType_Count; ++i) {
        const bool selected = i == typeIndex;
        if (ImGui::Selectable(specFor(i).label, selected)) {
          typeIndex = i;
          useSource1TimeAsX = true;
          resetPendingSourcesForType();
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    if (spec.is3D && ImGui::Checkbox("Use Source 1 Time as X", &useSource1TimeAsX)) {
      resetPendingSourcesForType();
    }

    if (!spec.is3D && spec.maxSources > spec.minSources) {
      if (ImGui::Button("Add Signal") &&
          static_cast<int>(pendingSources.size()) < spec.maxSources) {
        pendingSources.push_back({});
      }
      ImGui::SameLine();
      if (ImGui::Button("Remove Signal") &&
          static_cast<int>(pendingSources.size()) > spec.minSources) {
        pendingSources.pop_back();
        activeSourceIndex = std::clamp(activeSourceIndex, 0,
                                       std::max(0, static_cast<int>(pendingSources.size()) - 1));
      }
    }

    if (ImGui::BeginChild("##plot_preview", ImVec2(0.0f, 280.0f), true)) {
      PlotWindow preview{};
      preview.id = -1;
      preview.typeIndex = typeIndex;
      preview.sources = pendingSources;
      preview.useSource1TimeAsX = useSource1TimeAsX;
      PlotRenderer::render(arena, preview);
    }
    ImGui::EndChild();

    ImGui::TableSetColumnIndex(1);
    ImGui::Text("Minimum Sources: %d",
                spec.is3D ? required3DSources(useSource1TimeAsX) : spec.minSources);
    ImGui::Text("Maximum Sources: %d",
                spec.is3D ? required3DSources(useSource1TimeAsX) : spec.maxSources);
    ImGui::SeparatorText("Assigned Sources");
    for (size_t i = 0; i < pendingSources.size(); ++i) {
      const bool selected = static_cast<int>(i) == activeSourceIndex;
      const std::string slotLabel = sourceSlotLabel(typeIndex, i, useSource1TimeAsX) + ": " +
                                    signalDisplayName(arena, pendingSources[i]);
      if (ImGui::Selectable(slotLabel.c_str(), selected)) activeSourceIndex = static_cast<int>(i);
    }

    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted("Search");
    if (creatorFocusSearch) {
      ImGui::SetKeyboardFocusHere();
      creatorFocusSearch = false;
    }
    if (ImGui::InputTextWithHint("##plot_search", "Search message, id, or signal", search,
                                 sizeof(search))) {
      refreshMatches();
    }
    if (ImGui::BeginChild("##plot_search_results", ImVec2(0.0f, 280.0f), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
      for (size_t i = 0; i < sourceMatches.size(); ++i) {
        const int optionIndex = sourceMatches[i];
        const bool selected = static_cast<int>(i) == selectedMatch;
        if (ImGui::Selectable(signalOptions[static_cast<size_t>(optionIndex)].label.c_str(),
                              selected, ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(maxSuggestionWidth + style.FramePadding.x * 2.0f, 0.0f))) {
          selectedMatch = static_cast<int>(i);
          pendingSources[static_cast<size_t>(activeSourceIndex)] =
              signalOptions[static_cast<size_t>(optionIndex)].ref;
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
              activeSourceIndex + 1 < static_cast<int>(pendingSources.size()))
            activeSourceIndex += 1;
        }
      }
    }

    ImGui::EndChild();

    ImGui::EndTable();
  }

  const bool canCreate = hasAllSources(pendingSources);
  if (!canCreate) ImGui::BeginDisabled();
  if (ImGui::Button("Create")) {
    createPlot();
    keepOpen = false;
  }
  if (!canCreate) ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) keepOpen = false;

  creatorOpen = keepOpen;
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);
}

void PlotManager::renderPlotWindows() {
  windows.erase(std::remove_if(windows.begin(), windows.end(),
                               [](const PlotWindow& window) { return !window.open; }),
                windows.end());
  for (PlotWindow& window : windows) {
    ImGui::PushID(window.id);
    ImGui::TextUnformatted(window.title.c_str());
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 52.0f);
    if (ImGui::SmallButton("Close")) window.open = false;
    if (ImGui::BeginChild("##plot_body", ImVec2(-1.0f, 340.0f), ImGuiChildFlags_Borders)) {
      PlotRenderer::render(arena, window);
    }
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::Spacing();
  }
}

void PlotManager::createPlot() {
  if (!hasAllSources(pendingSources)) return;
  PlotWindow window{};
  window.id = nextPlotId++;
  window.typeIndex = typeIndex;
  window.sources = pendingSources;
  window.useSource1TimeAsX = useSource1TimeAsX;
  window.title = makePlotTitle(arena, typeIndex, pendingSources);
  windows.push_back(std::move(window));
}

PlotManager& plotManager() {
  static PlotManager manager{};
  return manager;
}
