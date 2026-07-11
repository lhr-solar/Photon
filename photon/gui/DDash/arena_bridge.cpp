#include "arena_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "../../parse/arena.hpp"

namespace {

void addFault(ui::AppState& state, const char* name,
             ui::FaultSeverity severity, const char* message) {
  state.faults.push_back(ui::Fault{name, message, severity, 0});
}

// Seconds since the message carrying signalName last had a frame appended
// (see Arena::appendFrame); -1 if the signal isn't in any loaded message.
double MessageAgeSecondsForSignal(Arena& arena, const char* signalName) {
  for (uint32_t id : arena.validIds) {
    Message* msg = arena.messages[id];
    if (!msg) continue;
    for (uint32_t s = 0; s < msg->signalCount; s++) {
      Signal* sig = msg->signals[s];
      if (sig && sig->name == signalName) {
        auto age = std::chrono::steady_clock::now() - msg->lastUpdated;
        return std::chrono::duration<double>(age).count();
      }
    }
  }
  return -1.0;
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
      void* data = nullptr;
      uint32_t dataBytes = 0;
      arena.read(id, s, &data, &dataBytes);
      const uint32_t count = dataBytes / sizeof(double);
      if (count > 0) state.signals[sig->name] = static_cast<const double*>(data)[count - 1];
    }
  }

  // Embedded-Sharepoint CarCAN uses the newer *_Pos_* names while the
  // dashboard still consumes the original pedal names. Keep the raw names
  // and expose compatibility aliases until the UI is migrated wholesale.
  const auto aliasSignal = [&](const char* alias, const char* source) {
    const auto it = state.signals.find(source);
    if (it != state.signals.end()) state.signals[alias] = it->second;
  };
  aliasSignal("AccelPedal_Main_Pos", "Accel_Pos_Main");
  aliasSignal("AccelPedal_Redundant_Pos", "Accel_Pos_Redundant");
  aliasSignal("BrakePedal_Main_Pos", "Brake_Pos_Main");
  aliasSignal("BrakePedal_Redundant_Pos", "Brake_Pos_Redundant");
  aliasSignal("AccelPedal_Main_Fault", "Accel_Pos_Main_Fault");
  aliasSignal("AccelPedal_Redundant_Fault", "Accel_Pos_Redundant_Fault");
  aliasSignal("BrakePedal_Main_Fault", "Brake_Pos_Main_Fault");
  aliasSignal("BrakePedal_Redundant_Fault", "Brake_Pos_Redundant_Fault");

  // Everything below needs more than one signal at once, or isn't a raw
  // signal at all — can't be a generic name lookup.

  double velocityMps = state.get("MC_VehicleVelocity");
  if (std::isfinite(velocityMps) && std::abs(velocityMps) <= 200.0) {
    constexpr double mpsToMph = 2.2369362920544;
    state.speed = static_cast<int>(std::lround(std::abs(velocityMps) * mpsToMph));
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
    if (idx >= 0 && idx < 256) {
      if ((int)state.moduleVoltages.size() <= idx) state.moduleVoltages.resize(idx + 1, 0.0f);
      if ((int)state.moduleTemps.size() <= idx) state.moduleTemps.resize(idx + 1, 0.0f);
      state.moduleVoltages[idx] = (float)state.get("BPS_Voltage_Tap_Data");
      state.moduleTemps[idx] = (float)state.get("BPS_Temperature_Tap_Data");
    }
  }

  // VCU_Status has no single consolidated fault signal on the real bus; it
  // splits fault state into six per-subsystem 1-bit flags.
  static constexpr const char* kVcuFaultSignals[] = {
      "VCU_BPS_FAULT_DETECTED",     "VCU_CONTROLS_FAULT_DETECTED", "VCU_MTR_FAULT_DETECTED",
      "VCU_PEDALS_FAULT_DETECTED",  "VCU_STEERING_FAULT_DETECTED", "VCU_OTHER_FAULT",
  };

  state.bpsMsgAgeSeconds = MessageAgeSecondsForSignal(arena, "BPS_Fault");
  const bool hasConsolidatedVcuFault = state.signals.count("VCU_Fault") != 0;
  state.vcuMsgAgeSeconds = MessageAgeSecondsForSignal(
      arena, hasConsolidatedVcuFault ? "VCU_Fault" : kVcuFaultSignals[0]);

  bool hasBps = state.signals.count("BPS_Fault") != 0;
  bool hasVcu = hasConsolidatedVcuFault || state.signals.count(kVcuFaultSignals[0]) != 0;
  double bpsF = state.get("BPS_Fault");
  double vcuF = state.get("VCU_Fault");
  if (!hasConsolidatedVcuFault)
    for (const char* sig : kVcuFaultSignals) vcuF += state.get(sig) != 0.0 ? 1.0 : 0.0;
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
