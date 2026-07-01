#include "arena_bridge.hpp"

#include <algorithm>
#include <cmath>

#include "../../parse/arena.hpp"

namespace {

void addFault(ui::AppState& state, const char* name,
             ui::FaultSeverity severity, const char* message) {
  state.faults.push_back(ui::Fault{name, message, severity, 0});
}

std::string joinFaultNames(const std::vector<ui::Fault>& faults) {
  std::string names;
  for (const auto& fault : faults) {
    if (!names.empty()) names += ", ";
    names += fault.name;
  }
  return names;
}

std::string joinFaultMessages(const std::vector<ui::Fault>& faults) {
  std::string messages;
  for (const auto& fault : faults) {
    if (!messages.empty()) messages += "; ";
    messages += fault.name + ": " + fault.message;
  }
  return messages;
}

}  // namespace

namespace ui {

void UpdateDashboardState(Arena& arena, AppState& state) {
  state.heartbeat = static_cast<uint8_t>((state.heartbeat + 1) % 256);
  state.faults.clear();

  // Mirror every currently-loaded DBC signal into the generic map, keyed by
  // its own name. dashboard.cpp reads directly from this by DBC name; no
  // hand-maintained signal list here.
  state.signals.clear();
  for (uint32_t id : arena.validIds) {
    Message* msg = arena.messages[id];
    if (!msg) continue;
    for (uint32_t s = 0; s < msg->signalCount; s++) {
      Signal* sig = msg->signals[s];
      if (!sig) continue;
      double value = 0.0;
      if (arena.readByName(sig->name, value)) state.signals[sig->name] = value;
    }
  }

  // Everything below needs more than one signal at once, or isn't a raw
  // signal at all — can't be a generic name lookup.

  double velocityMps = state.get("MC_VehicleVelocity");
  if (std::isfinite(velocityMps) && std::abs(velocityMps) <= 200.0) {
    constexpr double mpsToMph = 2.2369362920544;
    state.speed = static_cast<int>(std::lround(std::max(0.0, velocityMps * mpsToMph)));
  }

  if (state.getBool("Gear_Forward")) state.gear = Gear::Forward;
  if (state.getBool("Gear_Reverse")) state.gear = Gear::Reverse;
  if (state.getBool("Gear_Neutral")) state.gear = Gear::Neutral;

  if (state.getBool("Blinker_Left")) state.turnSignal = TurnSignal::Left;
  else if (state.getBool("Blinker_Right")) state.turnSignal = TurnSignal::Right;
  else state.turnSignal = TurnSignal::None;

  state.brakeEngaged = state.get("BrakePedal_Main_Pos") > 5.0 ||
                        state.get("Brake_Pressure_1") > 10.0 ||
                        state.get("Brake_Pressure_2") > 10.0;

  if (state.signals.count("BPS_Tap_idx")) {
    int idx = (int)state.get("BPS_Tap_idx");
    if ((int)state.moduleVoltages.size() <= idx) state.moduleVoltages.resize(idx + 1, 0.0f);
    if ((int)state.moduleTemps.size() <= idx) state.moduleTemps.resize(idx + 1, 0.0f);
    state.moduleVoltages[idx] = (float)state.get("BPS_Voltage_Tap_Data");
    state.moduleTemps[idx] = (float)state.get("BPS_Temperature_Tap_Data");
  }

  bool hasBps = state.signals.count("BPS_Fault") != 0;
  bool hasVcu = state.signals.count("VCU_Fault") != 0;
  double bpsF = state.get("BPS_Fault");
  double vcuF = state.get("VCU_Fault");
  if (hasBps && bpsF != 0) addFault(state, "BPS", FaultSeverity::Critical, "BPS fault detected");
  if (hasVcu && vcuF != 0) addFault(state, "VCU", FaultSeverity::Critical, "VCU fault detected");
  std::stable_sort(state.faults.begin(), state.faults.end(), [](const Fault& a, const Fault& b) {
    return (a.name == "BPS" ? 0 : a.name == "VCU" ? 1 : 2) <
           (b.name == "BPS" ? 0 : b.name == "VCU" ? 1 : 2);
  });
  state.canFault = (hasBps && bpsF != 0) || (hasVcu && vcuF != 0);
  state.canFaultId = 0;
  state.canFaultName = state.canFault ? joinFaultNames(state.faults) : std::string{};
  state.canFaultMessage = state.canFault ? joinFaultMessages(state.faults) : std::string{};
}

}  // namespace ui
