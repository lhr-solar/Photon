#include "customViewDocument.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#include "json.hpp"

namespace {
using Json = nlohmann::json;

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

Json sourceToJson(const PlotManager::PlotSourceRef& source) {
  Json value = {{"messageId", source.messageId}, {"signalIndex", source.signalIndex}};
  if (!source.messageName.empty()) value["messageName"] = source.messageName;
  if (!source.signalName.empty()) value["signalName"] = source.signalName;
  if (!source.label.empty()) value["label"] = source.label;
  if (source.scale != 1.0) value["scale"] = source.scale;
  if (source.offset != 0.0) value["offset"] = source.offset;
  return value;
}

Json plotToJson(const PlotManager::PlotWindow& plot) {
  Json sources = Json::array();
  for (const auto& source : plot.sources) sources.push_back(sourceToJson(source));
  return {{"type", PlotManager::typeKey(plot.typeIndex)},
          {"title", plot.title},
          {"useSource1TimeAsX", plot.useSource1TimeAsX},
          {"timeWindowSeconds", plot.timeWindowSeconds},
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

Json canMonitorToJson(const CustomViewCanMonitor& monitor) {
  return {{"title", monitor.title},
          {"filter", monitor.filter},
          {"recordPath", monitor.recordPath},
          {"sort", monitor.sort}};
}

Json frontCamToJson(const CustomViewFrontCam& cam) { return {{"title", cam.title}}; }

Json scene3DToJson(const CustomViewScene3D& scene) { return {{"title", scene.title}}; }

const char* widgetKindKey(CustomViewWidgetKind kind) {
  switch (kind) {
    case CustomViewWidgetKind::CellGrid:
      return "cell-grid";
    case CustomViewWidgetKind::Watchdog:
      return "watchdog";
    case CustomViewWidgetKind::CanMonitor:
      return "can-monitor";
    case CustomViewWidgetKind::FrontCam:
      return "front-cam";
    case CustomViewWidgetKind::Scene3D:
      return "scene-3d";
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
  else if (widget.kind == CustomViewWidgetKind::CanMonitor)
    value["canMonitor"] = canMonitorToJson(widget.canMonitor);
  else if (widget.kind == CustomViewWidgetKind::FrontCam)
    value["frontCam"] = frontCamToJson(widget.frontCam);
  else if (widget.kind == CustomViewWidgetKind::Scene3D)
    value["scene3D"] = scene3DToJson(widget.scene3D);
  else
    value["plot"] = plotToJson(widget.plot);
  return value;
}

Json panelToJson(const CustomViewDefinition& panel) {
  Json widgets = Json::array();
  for (const auto& widget : panel.widgets) widgets.push_back(widgetToJson(widget));
  return {
      {"id", panel.id.empty() ? "panel" : panel.id},
      {"name", panel.name.empty() ? "Panel" : panel.name},
      {"layout", {{"columns", panel.columns}, {"rowHeight", panel.rowHeight}, {"gap", panel.gap}}},
      {"widgets", std::move(widgets)}};
}

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
    const bool canSplit =
        std::all_of(panel.widgets.begin(), panel.widgets.end(),
                    [](const auto& w) { return w.rect.height * 2 <= kMaxHeight; });
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

CustomViewDefinition parsePanel(const Json& root, int& plotId) {
  CustomViewDefinition panel{};
  panel.schemaVersion = 1;
  panel.id = root.value("id", "panel-" + std::to_string(plotId));
  panel.name = root.value("name", "Panel");
  const Json layout = root.value("layout", Json::object());
  panel.columns = std::clamp(layout.value("columns", 48), 1, 48);
  panel.rowHeight = std::clamp(layout.value("rowHeight", 48.0f), 48.0f, 1200.0f);
  panel.gap = std::clamp(layout.value("gap", 8.0f), 0.0f, 64.0f);

  std::unordered_set<std::string> ids{};
  for (const Json& widgetJson : root.value("widgets", Json::array())) {
    const std::string kind = widgetJson.value("kind", "");
    if (kind != "plot" && kind != "cell-grid" && kind != "watchdog" && kind != "can-monitor" &&
        kind != "front-cam" && kind != "scene-3d")
      continue;

    CustomViewWidget widget{};
    widget.id = widgetJson.value("id", "widget-" + std::to_string(plotId));
    if (!ids.insert(widget.id).second)
      throw std::runtime_error("duplicate widget id: " + widget.id);
    const Json rect = widgetJson.value("rect", Json::object());
    widget.rect.x = std::max(0, rect.value("x", 0));
    widget.rect.y = std::max(0, rect.value("y", 0));
    widget.rect.width = std::clamp(rect.value("w", panel.columns), 1, panel.columns);
    widget.rect.height = std::clamp(rect.value("h", 6), 1, 48);
    if (widget.rect.x + widget.rect.width > panel.columns)
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
      panel.widgets.push_back(std::move(widget));
      continue;
    }

    if (kind == "watchdog") {
      widget.kind = CustomViewWidgetKind::Watchdog;
      const Json watchdogJson = widgetJson.at("watchdog");
      widget.watchdog.title = watchdogJson.value("title", widget.id);
      widget.watchdog.message = watchdogJson.value("message", "Signal out of range");
      widget.watchdog.unit = watchdogJson.value("unit", "");
      widget.watchdog.comparison =
          watchdogCompareFromKey(watchdogJson.value("comparison", "below"));
      widget.watchdog.threshold = watchdogJson.value("threshold", 0.0);
      widget.watchdog.hideWhenOk = watchdogJson.value("hideWhenOk", true);
      const Json sourceJson = watchdogJson.value("source", Json::object());
      if (sourceJson.find("messageId") != sourceJson.end())
        widget.watchdog.source.messageId = parseMessageId(sourceJson.at("messageId"));
      widget.watchdog.source.messageName = sourceJson.value("messageName", "");
      widget.watchdog.source.signalName = sourceJson.value("signalName", "");
      widget.watchdog.source.signalIndex = sourceJson.value("signalIndex", SIGNAL_MAX);
      widget.watchdog.source.assigned = true;
      panel.widgets.push_back(std::move(widget));
      continue;
    }

    if (kind == "can-monitor") {
      widget.kind = CustomViewWidgetKind::CanMonitor;
      const Json monitorJson = widgetJson.value("canMonitor", Json::object());
      widget.canMonitor.title = monitorJson.value("title", widget.id);
      widget.canMonitor.filter = monitorJson.value("filter", "");
      widget.canMonitor.recordPath = monitorJson.value("recordPath", "views/can-capture.log");
      widget.canMonitor.sort = std::clamp(monitorJson.value("sort", 0), 0, 3);
      panel.widgets.push_back(std::move(widget));
      continue;
    }

    if (kind == "front-cam") {
      widget.kind = CustomViewWidgetKind::FrontCam;
      const Json camJson = widgetJson.value("frontCam", Json::object());
      widget.frontCam.title = camJson.value("title", "Front Camera");
      panel.widgets.push_back(std::move(widget));
      continue;
    }

    if (kind == "scene-3d") {
      widget.kind = CustomViewWidgetKind::Scene3D;
      const Json sceneJson = widgetJson.value("scene3D", Json::object());
      widget.scene3D.title = sceneJson.value("title", "3D View");
      panel.widgets.push_back(std::move(widget));
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
    widget.plot.timeWindowSeconds =
        std::clamp(plotJson.value("timeWindowSeconds", PlotManager::kDefaultTimeWindowSeconds),
                   PlotManager::kMinTimeWindowSeconds, PlotManager::kMaxTimeWindowSeconds);
    for (const Json& sourceJson : plotJson.value("sources", Json::array())) {
      PlotManager::PlotSourceRef source{};
      source.messageId = parseMessageId(sourceJson.at("messageId"));
      source.messageName = sourceJson.value("messageName", "");
      source.signalName = sourceJson.value("signalName", "");
      source.signalIndex = sourceJson.value("signalIndex", SIGNAL_MAX);
      source.label = sourceJson.value("label", "");
      source.scale = sourceJson.value("scale", 1.0);
      source.offset = sourceJson.value("offset", 0.0);
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
    panel.widgets.push_back(std::move(widget));
  }

  if (panel.widgets.size() > 128) throw std::runtime_error("panel exceeds 128 widget limit");
  densifyPanelGrid(panel);
  return panel;
}
}  // namespace

CustomViewDocumentState CustomViewDocument::parse(std::string_view text) {
  const Json root = Json::parse(text);
  if (root.value("schemaVersion", 0) != 1)
    throw std::runtime_error("unsupported schemaVersion; expected 1");

  CustomViewDocumentState state{};
  int plotId = 10000;
  if (root.find("panels") != root.end() && root["panels"].is_array() && !root["panels"].empty()) {
    for (const Json& panelJson : root["panels"])
      state.panels.push_back(parsePanel(panelJson, plotId));
  } else {
    state.panels.push_back(parsePanel(root, plotId));
  }
  if (state.panels.size() > 32) throw std::runtime_error("workspace exceeds 32 panel limit");

  state.activePanel =
      std::clamp(root.value("activePanel", 0), 0, static_cast<int>(state.panels.size()) - 1);
  state.nextWidgetPlotId = plotId;
  state.nextPanelId = static_cast<int>(state.panels.size()) + 1;
  for (const auto& panel : state.panels) {
    if (panel.id.rfind("panel-", 0) != 0) continue;
    try {
      state.nextPanelId = std::max(state.nextPanelId, std::stoi(panel.id.substr(6)) + 1);
    } catch (const std::invalid_argument&) {
    } catch (const std::out_of_range&) {
    }
  }
  return state;
}

std::string CustomViewDocument::serialize(const std::vector<CustomViewDefinition>& panels,
                                          int activePanel) {
  Json panelList = Json::array();
  for (const auto& panel : panels) panelList.push_back(panelToJson(panel));
  const Json root = {{"$schema", "../docs/config/photon-view.schema.json"},
                     {"schemaVersion", 1},
                     {"id", "custom-views"},
                     {"name", "Custom Views"},
                     {"activePanel", activePanel},
                     {"panels", std::move(panelList)}};
  return root.dump(2);
}
