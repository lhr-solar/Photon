#include "customViewTelemetry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace {
Message* findMessage(Arena& arena, uint32_t messageId) {
  if (messageId >= arena.messages.size()) return nullptr;
  return arena.messages[messageId];
}

uint32_t findSignalIndex(Message* message, std::string_view signalName) {
  if (!message || signalName.empty()) return SIGNAL_MAX;
  for (uint32_t index = 0; index < message->signalCount; ++index) {
    if (message->signals[index] && message->signals[index]->name == signalName) return index;
  }
  return SIGNAL_MAX;
}

bool readSeries(Arena& arena, uint32_t messageId, uint32_t signalIndex, const double*& values,
                int& count) {
  values = nullptr;
  count = 0;
  if (signalIndex >= SIGNAL_MAX) return false;
  void* data = nullptr;
  uint32_t bytes = 0;
  arena.read(messageId, signalIndex, &data, &bytes);
  const int samples = static_cast<int>(bytes / sizeof(double));
  if (samples <= 0 || !data) return false;
  values = static_cast<const double*>(data);
  count = samples;
  return true;
}

bool readLatest(Arena& arena, uint32_t messageId, uint32_t signalIndex, double& value) {
  const double* values = nullptr;
  int count = 0;
  if (!readSeries(arena, messageId, signalIndex, values, count)) return false;
  value = values[count - 1];
  return true;
}
}  // namespace

void CustomViewTelemetry::resolve(Arena& arena, PlotManager::PlotSourceRef& source) {
  source.assigned = false;
  Message* message = findMessage(arena, source.messageId);
  if (!message && !source.messageName.empty()) {
    for (uint32_t id : arena.validIds) {
      Message* candidate = findMessage(arena, id);
      if (candidate && candidate->name == source.messageName) {
        message = candidate;
        source.messageId = id;
        break;
      }
    }
  }
  if (!message) return;

  uint32_t index = findSignalIndex(message, source.signalName);
  if (index == SIGNAL_MAX && source.signalName.empty() &&
      source.signalIndex < message->signalCount && message->signals[source.signalIndex])
    index = source.signalIndex;
  if (index == SIGNAL_MAX) return;

  source.signalIndex = index;
  source.messageName = message->name;
  source.signalName = message->signals[index]->name;
  source.assigned = true;
}

void CustomViewTelemetry::resolve(Arena& arena, CustomViewCellGrid& grid) {
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

  Message* voltageMessage = findMessage(arena, grid.voltageMessageId);
  Message* temperatureMessage = findMessage(arena, grid.temperatureMessageId);
  Message* statusMessage = findMessage(arena, grid.statusMessageId);

  grid.voltageTapIdx = findSignalIndex(voltageMessage, "BPS_Tap_idx");
  grid.voltageDataIdx = findSignalIndex(voltageMessage, "BPS_Voltage_Tap_Data");
  grid.voltageFaultIdx = findSignalIndex(voltageMessage, "BPS_Voltage_Tap_Fault");
  grid.voltageAgeIdx = findSignalIndex(voltageMessage, "BPS_Voltage_Tap_Age");
  grid.temperatureTapIdx = findSignalIndex(temperatureMessage, "BPS_Tap_idx");
  grid.temperatureDataIdx = findSignalIndex(temperatureMessage, "BPS_Temperature_Tap_Data");
  grid.temperatureFaultIdx = findSignalIndex(temperatureMessage, "BPS_Temperature_Tap_Fault");
  grid.temperatureAgeIdx = findSignalIndex(temperatureMessage, "BPS_Temperature_Tap_Age");
  grid.statusPackVoltageIdx = findSignalIndex(statusMessage, "Main_Battery_Voltage");
  grid.statusAvgTempIdx = findSignalIndex(statusMessage, "Main_Battery_Avg_Temperature");
  grid.statusFaultIdx = findSignalIndex(statusMessage, "BPS_Fault");

  grid.resolved = grid.voltageTapIdx != SIGNAL_MAX && grid.voltageDataIdx != SIGNAL_MAX &&
                  grid.temperatureTapIdx != SIGNAL_MAX && grid.temperatureDataIdx != SIGNAL_MAX;
}

void CustomViewTelemetry::update(Arena& arena, CustomViewCellGrid& grid) {
  for (auto& cell : grid.cells) cell = {};
  grid.packVoltage = 0.0;
  grid.packAvgTemp = 0.0;
  grid.packFault = 0.0;
  grid.hasPackVoltage = false;
  grid.hasPackAvgTemp = false;
  grid.hasPackFault = false;
  if (!grid.resolved) return;

  const double* voltageTap = nullptr;
  const double* voltageData = nullptr;
  const double* voltageFault = nullptr;
  const double* voltageAge = nullptr;
  int voltageTapCount = 0;
  int voltageDataCount = 0;
  int voltageFaultCount = 0;
  int voltageAgeCount = 0;
  readSeries(arena, grid.voltageMessageId, grid.voltageTapIdx, voltageTap, voltageTapCount);
  readSeries(arena, grid.voltageMessageId, grid.voltageDataIdx, voltageData, voltageDataCount);
  readSeries(arena, grid.voltageMessageId, grid.voltageFaultIdx, voltageFault, voltageFaultCount);
  readSeries(arena, grid.voltageMessageId, grid.voltageAgeIdx, voltageAge, voltageAgeCount);
  const int voltageSamples = std::min(voltageTapCount, voltageDataCount);
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
  readSeries(arena, grid.temperatureMessageId, grid.temperatureTapIdx, temperatureTap,
             temperatureTapCount);
  readSeries(arena, grid.temperatureMessageId, grid.temperatureDataIdx, temperatureData,
             temperatureDataCount);
  readSeries(arena, grid.temperatureMessageId, grid.temperatureFaultIdx, temperatureFault,
             temperatureFaultCount);
  readSeries(arena, grid.temperatureMessageId, grid.temperatureAgeIdx, temperatureAge,
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
      readLatest(arena, grid.statusMessageId, grid.statusPackVoltageIdx, grid.packVoltage);
  grid.hasPackAvgTemp =
      readLatest(arena, grid.statusMessageId, grid.statusAvgTempIdx, grid.packAvgTemp);
  grid.hasPackFault = readLatest(arena, grid.statusMessageId, grid.statusFaultIdx, grid.packFault);
}

void CustomViewTelemetry::resolve(Arena& arena, CustomViewWatchdog& watchdog) {
  resolve(arena, watchdog.source);
  if (!watchdog.source.assigned) return;
  Message* message = findMessage(arena, watchdog.source.messageId);
  if (!message || watchdog.source.signalIndex >= message->signalCount) return;
  if (watchdog.unit.empty() || watchdog.unit == "NULL") {
    const std::string& unit = message->signals[watchdog.source.signalIndex]->unit;
    if (!unit.empty() && unit != "NULL") watchdog.unit = unit;
  }
}

void CustomViewTelemetry::update(Arena& arena, CustomViewWatchdog& watchdog) {
  watchdog.hasValue = false;
  watchdog.tripped = false;
  if (!watchdog.source.assigned) return;
  double value = 0.0;
  if (!readLatest(arena, watchdog.source.messageId, watchdog.source.signalIndex, value)) return;
  watchdog.latest = value;
  watchdog.hasValue = true;
  watchdog.tripped = watchdog.comparison == CustomViewWatchdogCompare::Above
                         ? value > watchdog.threshold
                         : value < watchdog.threshold;
}
