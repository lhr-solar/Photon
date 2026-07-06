#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

enum class FaultSeverity { Info, Warning, Critical };

enum class Gear { Forward, Neutral, Reverse };

enum class TurnSignal { None, Left, Right };

struct Fault {
  std::string name;
  std::string message;
  FaultSeverity severity;
  int64_t timestamp;  // Unix timestamp in milliseconds
};

/**
 * Dashboard state. Raw CAN telemetry is read directly from `signals` by DBC
 * signal name (see get()/getBool()) rather than being copied into named
 * fields — only values that can't be a single raw signal (unit conversions,
 * multi-signal derivations, UI-only state) get dedicated fields below.
 */
struct AppState {
  std::unordered_map<std::string, double> signals;

  double get(const std::string& name, double fallback = 0.0) const {
    auto it = signals.find(name);
    return it != signals.end() ? it->second : fallback;
  }

  bool getBool(const std::string& name, bool fallback = false) const {
    auto it = signals.find(name);
    return it != signals.end() ? it->second != 0.0 : fallback;
  }

  // Derived from MC_VehicleVelocity (m/s), converted to mph.
  int speed = 0;

  // Derived from Gear_Forward/Gear_Reverse/Gear_Neutral (mutually exclusive bits).
  Gear gear = Gear::Neutral;

  // Derived from Blinker_Left/Blinker_Right, with left taking precedence.
  TurnSignal turnSignal = TurnSignal::None;

  // Derived from BrakePedal_Main_Pos and Brake_Pressure_{1,2} thresholds.
  bool brakeEngaged = false;

  // BPS_Voltage_Tap_Data / BPS_Temperature_Tap_Data unpacked by BPS_Tap_idx.
  std::vector<float> moduleVoltages;
  std::vector<float> moduleTemps;

  // Built from BPS_Fault / VCU_Fault.
  std::vector<Fault> faults;
  bool canFault = false;
  uint16_t canFaultId = 0;
  std::string canFaultName;
  std::string canFaultMessage;
  bool canFaultRecoverable = false;
  uint16_t canFaultRecoverableId = 0;
  std::string canFaultRecoverableName;
  std::string canFaultRecoverableMessage;

  // Seconds since the message carrying BPS_Fault / VCU_Fault last updated
  // in the arena; -1 = never seen. Lets the dashboard flag a dropped CAN
  // link instead of silently showing a stale "no fault" state forever.
  double bpsMsgAgeSeconds = -1.0;
  double vcuMsgAgeSeconds = -1.0;

  // UI-only state, not CAN data.
  uint8_t heartbeat = 0;
  bool showDebugScreen = false;
  void* leftCameraTexture = nullptr;
  void* rightCameraTexture = nullptr;
  void* rearCameraTexture = nullptr;
};

inline AppState CreateDefaultState() { return AppState{}; }

inline const char* GearToString(Gear gear) {
  switch (gear) {
    case Gear::Forward:
      return "F";
    case Gear::Neutral:
      return "N";
    case Gear::Reverse:
      return "R";
    default:
      return "?";
  }
}

inline const char* GearToLongString(Gear gear) {
  switch (gear) {
    case Gear::Forward:
      return "F - Forward";
    case Gear::Neutral:
      return "N - Neutral";
    case Gear::Reverse:
      return "R - Reverse";
    default:
      return "?";
  }
}

inline Gear GearFromIndex(int index) {
  switch (index) {
    case 0:
      return Gear::Forward;
    case 1:
      return Gear::Neutral;
    case 2:
      return Gear::Reverse;
    default:
      return Gear::Neutral;
  }
}

}  // namespace ui
