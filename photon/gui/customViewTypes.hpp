#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../parse/arena.hpp"
#include "plots.hpp"

enum class CustomViewWidgetKind { Plot, CellGrid, Watchdog, CanMonitor, FrontCam, Scene3D };

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

struct CustomViewCanMonitor {
  std::string title = "CAN Monitor";
  std::string filter{};
  std::string recordPath = "views/can-capture.log";
  int sort = 0;
  uint32_t selectedId = UINT32_MAX;
};

struct CustomViewFrontCam {
  std::string title = "Front Camera";
};

struct CustomViewScene3D {
  std::string title = "3D View";
};

struct CustomViewWidget {
  std::string id{};
  CustomViewWidgetKind kind = CustomViewWidgetKind::Plot;
  CustomViewRect rect{};
  PlotManager::PlotWindow plot{};
  CustomViewCellGrid cellGrid{};
  CustomViewWatchdog watchdog{};
  CustomViewCanMonitor canMonitor{};
  CustomViewFrontCam frontCam{};
  CustomViewScene3D scene3D{};
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
