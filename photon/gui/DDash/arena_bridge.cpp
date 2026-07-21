#include "arena_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "../../parse/arena.hpp"

namespace {

constexpr char kQualifiedSignalSeparator = '.';

std::string qualifiedSignalName(const Message& message, const Signal& signal) {
  return message.name + kQualifiedSignalSeparator + signal.name;
}

void aliasSignal(ui::AppState& state, const char* alias, const char* source) {
  const auto it = state.signals.find(source);
  if (it != state.signals.end()) state.signals[alias] = it->second;
}

void aliasMessageSignal(ui::AppState& state, const char* alias, const char* message,
                        const char* signal) {
  const std::string source = std::string(message) + kQualifiedSignalSeparator + signal;
  aliasSignal(state, alias, source.c_str());
}

void addFault(ui::AppState& state, const char* name,
             ui::FaultSeverity severity, const char* message) {
  state.faults.push_back(ui::Fault{name, message, severity, 0});
}

struct MotorFaultDefinition {
  const char* signal;
  const char* message;
};

// MotorCAN.dbc, MC_Status (CAN ID 0x421 / 1057), bits 16-24.
constexpr MotorFaultDefinition kMotorFaults[] = {
    {"MC_FAULT_HardwareOverCurrent", "Hardware overcurrent"},
    {"MC_FAULT_SoftwareOverCurrent", "Software overcurrent"},
    {"MC_FAULT_DcBusOverVoltage", "DC bus overvoltage"},
    {"MC_FAULT_BadMotorPositionHallSeq", "Bad motor position Hall sequence"},
    {"MC_FAULT_WatchdogCausedLastReset", "Watchdog caused last reset"},
    {"MC_FAULT_ConfigRead", "Configuration read fault"},
    {"MC_FAULT_15vRailUnderVoltage", "15 V rail undervoltage"},
    {"MC_FAULT_DesaturationFault", "Desaturation fault"},
    {"MC_FAULT_MotorOverSpeed", "Motor overspeed"},
};

struct MotorLimitDefinition {
  const char* signal;
  const char* message;
};

// MotorCAN.dbc, MC_Status (CAN ID 0x421 / 1057), bits 0-6.
constexpr MotorLimitDefinition kMotorLimits[] = {
    {"MC_LIMIT_OutputVoltagePWM", "Output voltage PWM"},
    {"MC_LIMIT_MotorCurrent", "Motor current"},
    {"MC_LIMIT_Velocity", "Velocity"},
    {"MC_LIMIT_BusCurrent", "Bus current"},
    {"MC_LIMIT_BusVoltageUpper", "Bus voltage upper limit"},
    {"MC_LIMIT_BusVoltageLower", "Bus voltage lower limit"},
    {"MC_LIMIT_MotorTemp", "IPM or motor temperature"},
};

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

std::string joinFaultNames(const std::vector<ui::Fault>& faults, ui::FaultSeverity severity) {
  std::string names;
  for (const auto& fault : faults) {
    if (fault.severity != severity) continue;
    if (!names.empty()) names += ", ";
    names += fault.name;
  }
  return names;
}

std::string joinFaultMessages(const std::vector<ui::Fault>& faults, ui::FaultSeverity severity) {
  std::string messages;
  for (const auto& fault : faults) {
    if (fault.severity != severity) continue;
    if (!messages.empty()) messages += "; ";
    messages += fault.name + ": " + fault.message;
  }
  return messages;
}

void updateBpsTapSeries(Arena& arena, const char* messageName, const char* valueName,
                        std::vector<float>& destination) {
  Message* message = nullptr;
  for (uint32_t id : arena.validIds) {
    Message* candidate = arena.messages[id];
    if (candidate && candidate->name == messageName) {
      message = candidate;
      break;
    }
  }
  if (!message) return;

  uint32_t tapSignal = SIGNAL_MAX;
  uint32_t valueSignal = SIGNAL_MAX;
  for (uint32_t signal = 0; signal < message->signalCount; ++signal) {
    if (!message->signals[signal]) continue;
    if (message->signals[signal]->name == "BPS_Tap_idx") tapSignal = signal;
    if (message->signals[signal]->name == valueName) valueSignal = signal;
  }
  if (tapSignal == SIGNAL_MAX || valueSignal == SIGNAL_MAX) return;

  void* tapData = nullptr;
  void* valueData = nullptr;
  uint32_t tapBytes = 0;
  uint32_t valueBytes = 0;
  arena.read(message->id, tapSignal, &tapData, &tapBytes);
  arena.read(message->id, valueSignal, &valueData, &valueBytes);
  const uint32_t count = std::min(tapBytes, valueBytes) / sizeof(double);
  if (count == 0 || !tapData || !valueData) return;

  const auto* taps = static_cast<const double*>(tapData);
  const auto* values = static_cast<const double*>(valueData);
  bool filled[32]{};
  destination.resize(32, 0.0f);
  for (uint32_t offset = 0; offset < count; ++offset) {
    const uint32_t sample = count - offset - 1;
    const int tap = static_cast<int>(std::lround(taps[sample]));
    if (tap < 0 || tap >= 32 || filled[tap] || !std::isfinite(values[sample])) continue;
    destination[static_cast<size_t>(tap)] = static_cast<float>(values[sample]);
    filled[tap] = true;
  }
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
      if (count == 0) continue;
      const double value = static_cast<const double*>(data)[count - 1];
      state.signals[qualifiedSignalName(*msg, *sig)] = value;
      state.signals[sig->name] = value;
    }
  }

  // Embedded-Sharepoint CarCAN uses the newer *_Pos_* names while the
  // dashboard still consumes the original pedal names. Keep the raw names
  // and expose compatibility aliases until the UI is migrated wholesale.
  aliasSignal(state, "AccelPedal_Main_Pos", "Accel_Pos_Main");
  aliasSignal(state, "AccelPedal_Redundant_Pos", "Accel_Pos_Redundant");
  aliasSignal(state, "BrakePedal_Main_Pos", "Brake_Pos_Main");
  aliasSignal(state, "BrakePedal_Redundant_Pos", "Brake_Pos_Redundant");
  aliasSignal(state, "AccelPedal_Main_Fault", "Accel_Pos_Main_Fault");
  aliasSignal(state, "AccelPedal_Redundant_Fault", "Accel_Pos_Redundant_Fault");
  aliasSignal(state, "BrakePedal_Main_Fault", "Brake_Pos_Main_Fault");
  aliasSignal(state, "BrakePedal_Redundant_Fault", "Brake_Pos_Redundant_Fault");

  // Some HighNoon messages intentionally repeat a signal name. Preserve the
  // generic value above for plots, while exposing stable dashboard aliases
  // from the message-qualified Arena values.
  aliasMessageSignal(state, "Brake_Pressure_1", "Brake_Pressure_1", "Brake_Pressure");
  aliasMessageSignal(state, "Brake_Pressure_2", "Brake_Pressure_2", "Brake_Pressure");
  aliasSignal(state, "SuppCharger_Status", "Supplemental_Charger_Status");
  aliasSignal(state, "Supplemental_DCDC_Voltage", "Supplemental_Vicor_Voltage");
  aliasSignal(state, "Supplemental_DCDC_Current", "Supplemental_Vicor_Current");

  uint8_t lightingFaults = 0;
  static constexpr const char* kLightingFaultSignals[] = {
      "LightingBoard_Front_Status", "LightingBoard_Left_Status",   "LightingBoard_Right_Status",
      "LightingBoard_Rear_Status",  "LightingBoard_Canopy_Status",
  };
  for (size_t index = 0; index < std::size(kLightingFaultSignals); ++index)
    if (state.getBool(kLightingFaultSignals[index])) lightingFaults |= 1u << index;
  state.signals["Controls_Lighting_Fault"] = lightingFaults;

  // Everything below needs more than one signal at once, or isn't a raw
  // signal at all — can't be a generic name lookup.

  double velocityMps = state.get("MC_VehicleVelocity");
  if (std::isfinite(velocityMps) && std::abs(velocityMps) <= 200.0) {
    constexpr double mpsToMph = 2.2369362920544;
    state.speed = static_cast<int>(std::lround(velocityMps * mpsToMph));
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

  // BPS_Tap_idx exists in voltage, temperature, ADC, fault, and balance messages. Pair the tap
  // index and value from the same canonical aggregate message instead of combining entries from
  // the flattened name-keyed signal map.
  updateBpsTapSeries(arena, "BPS_Voltage_Aggregate_Arr", "BPS_Voltage_Tap_Data",
                     state.moduleVoltages);
  updateBpsTapSeries(arena, "BPS_Temperature_Aggregate_Arr", "BPS_Temperature_Tap_Data",
                     state.moduleTemps);

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
  state.signals["VCU_Fault"] = vcuF;
  if (hasBps && bpsF != 0) addFault(state, "BPS", FaultSeverity::Critical, "BPS fault detected");

  uint16_t motorFaultMask = 0;
  for (size_t i = 0; i < sizeof(kMotorFaults) / sizeof(kMotorFaults[0]); ++i) {
    if (!state.getBool(kMotorFaults[i].signal)) continue;
    motorFaultMask |= static_cast<uint16_t>(1u << i);
    addFault(state, "Motor", FaultSeverity::Critical, kMotorFaults[i].message);
  }

  uint16_t motorLimitMask = 0;
  for (size_t i = 0; i < sizeof(kMotorLimits) / sizeof(kMotorLimits[0]); ++i) {
    if (!state.getBool(kMotorLimits[i].signal)) continue;
    motorLimitMask |= static_cast<uint16_t>(1u << i);
    addFault(state, "Motor Limit", FaultSeverity::Warning, kMotorLimits[i].message);
  }

  if (hasVcu && vcuF != 0) addFault(state, "VCU", FaultSeverity::Critical, "VCU fault detected");
  std::stable_sort(state.faults.begin(), state.faults.end(), [](const Fault& a, const Fault& b) {
    return (a.name == "BPS" ? 0 : a.name == "Motor" ? 1 : a.name == "VCU" ? 2 :
            a.name == "Motor Limit" ? 3 : 4) <
           (b.name == "BPS" ? 0 : b.name == "Motor" ? 1 : b.name == "VCU" ? 2 :
            b.name == "Motor Limit" ? 3 : 4);
  });
  state.canFault = (hasBps && bpsF != 0) || motorFaultMask != 0 || (hasVcu && vcuF != 0);
  state.canFaultId = motorFaultMask;
  if (hasBps && bpsF != 0) state.canFaultId |= 0x8000;
  if (hasVcu && vcuF != 0) state.canFaultId |= 0x4000;
  state.canFaultName =
      state.canFault ? joinFaultNames(state.faults, FaultSeverity::Critical) : std::string{};
  state.canFaultMessage =
      state.canFault ? joinFaultMessages(state.faults, FaultSeverity::Critical) : std::string{};
  state.canFaultRecoverable = motorLimitMask != 0;
  state.canFaultRecoverableId = motorLimitMask;
  state.canFaultRecoverableName = state.canFaultRecoverable ? "Motor Limit" : std::string{};
  state.canFaultRecoverableMessage = state.canFaultRecoverable
                                         ? joinFaultMessages(state.faults, FaultSeverity::Warning)
                                         : std::string{};
}

}  // namespace ui
