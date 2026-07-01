#include "arena_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <string>

#include "../../parse/arena.hpp"

// staging's DBC parser doesn't parse VAL_/CM_ (value tables/comments), so
// fault messages below use static fallback text instead of a DBC-enumerated
// description.

namespace {

bool readArenaSignal(Arena& arena, const std::string& name, double& outValue) {
  for (uint32_t id = 0; id < MESSAGE_MAX; id++) {
    Message* msg = arena.messages[id];
    if (!msg) continue;
    for (uint32_t s = 0; s < msg->signalCount; s++) {
      Signal* sig = msg->signals[s];
      if (!sig || sig->name != name) continue;
      void* data = nullptr;
      uint32_t dataBytes = 0;
      arena.read(id, s, &data, &dataBytes);
      if (!dataBytes) return false;
      const uint32_t count = dataBytes / sizeof(double);
      if (count == 0) return false;
      outValue = static_cast<const double*>(data)[count - 1];
      return true;
    }
  }
  return false;
}

bool readFirstArenaSignal(Arena& arena, std::initializer_list<const char*> signalNames,
                          double& outValue) {
  for (const char* signalName : signalNames) {
    if (readArenaSignal(arena, signalName, outValue)) return true;
  }
  return false;
}

bool updateDashboardSpeed(Arena& arena, ui::AppState& state) {
  double velocityMps = 0.0;
  if (!readFirstArenaSignal(arena, {"MC_VehicleVelocity", "VehicleVelocity"}, velocityMps)) {
    return false;
  }
  if (!std::isfinite(velocityMps)) return false;

  constexpr double maxReasonableVehicleSpeedMps = 200.0;
  if (std::abs(velocityMps) > maxReasonableVehicleSpeedMps) return false;

  constexpr double metersPerSecondToMilesPerHour = 2.2369362920544;
  state.speed = static_cast<int>(std::lround(std::max(0.0, velocityMps * metersPerSecondToMilesPerHour)));
  return true;
}

float normalizePercent(double rawValue) {
  if (!std::isfinite(rawValue)) return 0.0f;
  double pct = rawValue;
  if (pct >= 0.0 && pct <= 1.0) pct *= 100.0;
  return static_cast<float>(std::clamp(pct, 0.0, 100.0));
}

float percentFromVoltage(double voltage, double emptyVoltage, double fullVoltage) {
  if (!std::isfinite(voltage) || voltage <= 0.0 || fullVoltage <= emptyVoltage) return 0.0f;
  double pct = (voltage - emptyVoltage) * 100.0 / (fullVoltage - emptyVoltage);
  return static_cast<float>(std::clamp(pct, 0.0, 100.0));
}

float milliampsToAmps(double rawValue) {
  if (!std::isfinite(rawValue)) return 0.0f;
  return static_cast<float>(rawValue * 0.001);
}

bool updateBatterySoc(Arena& arena, ui::AppState& state) {
  double soc = 0.0;
  bool updated = false;

  if (readFirstArenaSignal(arena,
      {"Main_Battery_SOC", "Main_Battery_SoC", "BPS_SOC", "BPS_SoC",
       "BPS_State_Of_Charge", "Pack_SOC", "Battery_SOC", "State_Of_Charge"},
      soc)) {
    state.mainBattery.soc = normalizePercent(soc);
    updated = true;
  } else if (state.mainBattery.voltage > 0.0f) {
    state.mainBattery.soc = percentFromVoltage(state.mainBattery.voltage, 96.0, 134.4);
    updated = true;
  }

  if (readFirstArenaSignal(arena,
      {"Supplemental_Battery_SOC", "Supplemental_Battery_SoC", "Supp_Battery_SOC",
       "Aux_Battery_SOC", "LV_Battery_SOC"},
      soc)) {
    state.suppBattery.soc = normalizePercent(soc);
    updated = true;
  } else if (state.suppBattery.voltage > 0.0f) {
    state.suppBattery.soc = percentFromVoltage(state.suppBattery.voltage, 11.8, 13.6);
    updated = true;
  }

  return updated;
}

void addFault(ui::AppState& state, const char* name,
             ui::FaultSeverity severity = ui::FaultSeverity::Critical, const char* message = "") {
  state.faults.push_back(ui::Fault{name ? name : "Fault", message ? message : "", severity, 0});
}

std::string joinFaultNames(const std::vector<ui::Fault>& faults) {
  std::string names;
  for (const auto& fault : faults) {
    if (fault.name.empty()) continue;
    if (!names.empty()) names += ", ";
    names += fault.name;
  }
  return names;
}

std::string joinFaultMessages(const std::vector<ui::Fault>& faults) {
  std::string messages;
  for (const auto& fault : faults) {
    if (fault.message.empty()) continue;
    if (!messages.empty()) messages += "; ";
    if (!fault.name.empty()) {
      messages += fault.name;
      messages += ": ";
    }
    messages += fault.message;
  }
  return messages;
}

int faultPriority(const ui::Fault& fault) {
  if (fault.name == "BPS") return 0;
  if (fault.name == "VCU BPS") return 1;
  return 2;
}

void prioritizeFaults(ui::AppState& state) {
  std::stable_sort(state.faults.begin(), state.faults.end(),
      [](const ui::Fault& a, const ui::Fault& b) { return faultPriority(a) < faultPriority(b); });
}

bool envFlagEnabled(const char* name) {
  std::string value;
#ifdef _WIN32
  char* rawValue = nullptr;
  size_t rawValueLength = 0;
  if (_dupenv_s(&rawValue, &rawValueLength, name) != 0 || !rawValue) return false;
  value = rawValue;
  std::free(rawValue);
#else
  const char* rawValue = std::getenv(name);
  if (!rawValue) return false;
  value = rawValue;
#endif
  return !value.empty() && value != "0" && value != "false" && value != "FALSE" && value != "off" &&
         value != "OFF";
}

void applyFakeCanFaults(ui::AppState& state) {
  state.faults.clear();
  addFault(state, "BPS", ui::FaultSeverity::Critical, "Undervoltage");
  addFault(state, "VCU BPS", ui::FaultSeverity::Critical, "BPS fault detected");
  addFault(state, "VCU Motor", ui::FaultSeverity::Critical, "Motor fault detected");
  prioritizeFaults(state);

  state.canFault = true;
  state.canFaultId = 0;
  state.canFaultName = joinFaultNames(state.faults);
  state.canFaultMessage = joinFaultMessages(state.faults);
  state.bpsFaultCode = 2;
  state.vcuFaultCode = static_cast<uint8_t>((1u << 0) | (1u << 2));
  state.brakeEngaged = true;
  state.brakePressure = 420.0f;
  state.brakePressure1 = 418.0f;
  state.brakePressure2 = 422.0f;

  if (state.mainBattery.soc == 0.0f) {
    state.mainBattery.soc = 62.0f;
    state.mainBattery.voltage = 96.0f;
    state.mainBattery.current = -18.0f;
  }
  if (state.suppBattery.soc == 0.0f) {
    state.suppBattery.soc = 84.0f;
    state.suppBattery.voltage = 13.6f;
    state.suppBattery.current = 3.0f;
  }
}

}  // namespace

namespace ui {

void UpdateDashboardState(Arena& arena, AppState& state) {
  state.heartbeat = static_cast<uint8_t>((state.heartbeat + 1) % 256);
  state.faults.clear();

  double val = 0;
  updateDashboardSpeed(arena, state);
  if (readArenaSignal(arena, "Main_Battery_Voltage", val)) state.mainBattery.voltage = (float)val;
  if (readArenaSignal(arena, "Main_Battery_Current", val)) state.mainBattery.current = (float)val;
  if (readArenaSignal(arena, "Supplemental_Battery_Voltage", val)) state.suppBattery.voltage = (float)val;
  updateBatterySoc(arena, state);
  if (readFirstArenaSignal(arena, {"MC_HeatsinkTemp", "HeatsinkTemp"}, val)) state.motorController.heatsinkTemp = (float)val;
  if (readFirstArenaSignal(arena, {"MC_BusVoltage", "BusVoltage"}, val)) state.motorController.voltage = (float)val;
  if (readFirstArenaSignal(arena, {"MC_BusCurrent", "BusCurrent"}, val)) state.motorController.current = (float)val;

  if (readFirstArenaSignal(arena, {"MC_PhaseCurrentB", "PhaseCurrentB"}, val)) state.motorController.phaseCurrentB = (float)val;
  if (readFirstArenaSignal(arena, {"MC_PhaseCurrentC", "PhaseCurrentC"}, val)) state.motorController.phaseCurrentC = (float)val;
  if (readFirstArenaSignal(arena, {"MC_BEMFq", "BEMFq"}, val)) state.motorController.backEmfQ = (float)val;
  if (readFirstArenaSignal(arena, {"MC_BEMFd", "BEMFd"}, val)) state.motorController.backEmfD = (float)val;

  if (readFirstArenaSignal(arena, {"MC_LIMIT_OutputVoltagePWM", "LimitOutputVoltagePWM"}, val)) state.motorController.limitOutputVoltage = (val != 0);
  if (readFirstArenaSignal(arena, {"MC_LIMIT_MotorCurrent", "LimitMotorCurrent"}, val)) state.motorController.limitMotorCurrent = (val != 0);
  if (readFirstArenaSignal(arena, {"MC_LIMIT_Velocity", "LimitVelocity"}, val)) state.motorController.limitVelocity = (val != 0);
  if (readFirstArenaSignal(arena, {"MC_LIMIT_BusCurrent", "LimitBusCurrent"}, val)) state.motorController.limitBusCurrent = (val != 0);
  if (readFirstArenaSignal(arena, {"MC_LIMIT_BusVoltageUpper", "LimitBusVoltageUpper"}, val)) state.motorController.limitBusVoltageUpper = (val != 0);
  if (readFirstArenaSignal(arena, {"MC_LIMIT_BusVoltageLower", "LimitBusVoltageLower"}, val)) state.motorController.limitBusVoltageLower = (val != 0);
  if (readFirstArenaSignal(arena, {"MC_LIMIT_MotorTemp", "LimitIpmOrMotorTemp"}, val)) state.motorController.limitIpmOrMotorTemp = (val != 0);

  // Overall CAN-fault gate. McQueen's VCU_Status has individual
  // VCU_*_FAULT_DETECTED bits rather than a single VCU_Fault byte; OR the
  // unrecoverable/pipeline ones into a single "VCU faulted" signal.
  {
    double bpsF = 0;
    double legacyVcuF = 0;
    double vcuFaults[5] = {0};
    bool hasBps = readArenaSignal(arena, "BPS_Fault", bpsF);
    bool hasLegacyVcu = readArenaSignal(arena, "VCU_Fault", legacyVcuF);
    bool hasVcu = false;
    hasVcu |= readArenaSignal(arena, "VCU_BPS_FAULT_DETECTED", vcuFaults[0]);
    hasVcu |= readArenaSignal(arena, "VCU_CONTROLS_FAULT_DETECTED", vcuFaults[1]);
    hasVcu |= readArenaSignal(arena, "VCU_MTR_FAULT_DETECTED", vcuFaults[2]);
    hasVcu |= readArenaSignal(arena, "VCU_PEDALS_FAULT_DETECTED", vcuFaults[3]);
    hasVcu |= readArenaSignal(arena, "VCU_STEERING_FAULT_DETECTED", vcuFaults[4]);
    double vcuF = 0;
    for (double f : vcuFaults) vcuF += f;

    if (hasBps && bpsF != 0) addFault(state, "BPS", FaultSeverity::Critical, "BPS fault detected");
    if (hasLegacyVcu && legacyVcuF != 0) addFault(state, "VCU", FaultSeverity::Critical, "VCU fault detected");
    if (hasVcu) {
      if (vcuFaults[0] != 0) addFault(state, "VCU BPS", FaultSeverity::Critical, "BPS fault detected");
      if (vcuFaults[1] != 0) addFault(state, "VCU Controls", FaultSeverity::Critical, "Controls fault detected");
      if (vcuFaults[2] != 0) addFault(state, "VCU Motor", FaultSeverity::Critical, "Motor fault detected");
      if (vcuFaults[3] != 0) addFault(state, "VCU Pedals", FaultSeverity::Critical, "Pedals fault detected");
      if (vcuFaults[4] != 0) addFault(state, "VCU Steering", FaultSeverity::Critical, "Steering fault detected");
    }
    prioritizeFaults(state);
    if (hasBps && bpsF != 0) {
      state.canFault = true;
      state.canFaultId = 0;
      state.canFaultName = joinFaultNames(state.faults);
      state.canFaultMessage = joinFaultMessages(state.faults);
    } else if ((hasVcu && vcuF != 0) || (hasLegacyVcu && legacyVcuF != 0)) {
      state.canFault = true;
      state.canFaultId = 0;
      state.canFaultName = joinFaultNames(state.faults);
      state.canFaultMessage = joinFaultMessages(state.faults);
    } else {
      state.canFault = false;
      state.canFaultId = 0;
      state.canFaultName.clear();
      state.canFaultMessage.clear();
    }
  }

  if (readArenaSignal(arena, "HV_Plus_Contactor_State", val)) state.contactorStates.hvPositive = (val != 0);
  if (readArenaSignal(arena, "HV_Minus_Contactor_State", val)) state.contactorStates.hvNegative = (val != 0);
  if (readArenaSignal(arena, "Array_Precharge_Contactor_State", val)) state.contactorStates.arrayPrecharge = (val != 0);
  if (readArenaSignal(arena, "Array_Contactor_State", val)) state.contactorStates.arrayContactor = (val != 0);
  if (readArenaSignal(arena, "Motor_Precharge_Contactor_State", val)) state.contactorStates.motorPrecharge = (val != 0);
  if (readArenaSignal(arena, "Motor_Contactor_State", val)) state.contactorStates.motorContactor = (val != 0);

  if (readArenaSignal(arena, "Ignition_Off", val)) state.ignitionStates.lvEnabled = (val == 0);
  if (readArenaSignal(arena, "Ignition_Array", val)) state.ignitionStates.arrayEnabled = (val != 0);
  if (readArenaSignal(arena, "Ignition_Motor", val)) state.ignitionStates.motorEnabled = (val != 0);

  if (readArenaSignal(arena, "Gear_Forward", val) && val != 0) state.gear = Gear::Forward;
  if (readArenaSignal(arena, "Gear_Reverse", val) && val != 0) state.gear = Gear::Reverse;
  if (readArenaSignal(arena, "Gear_Neutral", val) && val != 0) state.gear = Gear::Neutral;

  {
    double bL = 0, bR = 0;
    if (readArenaSignal(arena, "Blinker_Left", bL) && bL != 0) state.turnSignal = TurnSignal::Left;
    else if (readArenaSignal(arena, "Blinker_Right", bR) && bR != 0) state.turnSignal = TurnSignal::Right;
    else state.turnSignal = TurnSignal::None;
  }

  if (readArenaSignal(arena, "Cruise_Enable", val)) state.cruise.enabled = (val != 0);
  if (readArenaSignal(arena, "Regen_Enable", val)) state.regenEnabled = (val != 0);
  if (readArenaSignal(arena, "BrakePedal_Main_Pos", val)) state.brakeEngaged = (val > 5.0);

  if (readArenaSignal(arena, "AccelPedal_Main_Pos", val)) state.pedalPercent = (float)val;
  if (readArenaSignal(arena, "Supplemental_Battery_Current", val)) state.suppBattery.current = milliampsToAmps(val);

  {
    double tapIdx = 0, tapV = 0, tapT = 0;
    if (readArenaSignal(arena, "BPS_Tap_idx", tapIdx)) {
      int idx = (int)tapIdx;
      if ((int)state.moduleVoltages.size() <= idx) state.moduleVoltages.resize(idx + 1, 0.0f);
      if ((int)state.moduleTemps.size() <= idx) state.moduleTemps.resize(idx + 1, 0.0f);
      if (readArenaSignal(arena, "BPS_Voltage_Tap_Data", tapV)) state.moduleVoltages[idx] = (float)tapV;
      if (readArenaSignal(arena, "BPS_Temperature_Tap_Data", tapT)) state.moduleTemps[idx] = (float)tapT;
    }
  }

  if (readArenaSignal(arena, "Main_Battery_Avg_Temperature", val)) state.mainBatteryAvgTemp = (float)val;
  if (readArenaSignal(arena, "BPS_Regen_OK", val)) state.bpsRegenOK = (val != 0);
  if (readArenaSignal(arena, "BPS_Charge_OK", val)) state.bpsChargeOK = (val != 0);
  if (readArenaSignal(arena, "BPS_Precharge_Battery_Voltage", val)) state.prechargeBatteryV = (float)val;
  if (readArenaSignal(arena, "BPS_Precharge_Array_Voltage", val)) state.prechargeArrayV = (float)val;
  if (readArenaSignal(arena, "Main_Battery_Current_RawV", val)) state.mainBatteryCurrentRawV = (float)val;
  if (readArenaSignal(arena, "BPS_Fault", val)) state.bpsFaultCode = (uint8_t)val;

  if (readArenaSignal(arena, "VCU_FSM_State", val)) state.vcuFsmState = (uint8_t)val;
  {
    uint8_t packed = 0;
    if (readArenaSignal(arena, "VCU_BPS_FAULT_DETECTED", val) && val != 0) packed |= (1u << 0);
    if (readArenaSignal(arena, "VCU_CONTROLS_FAULT_DETECTED", val) && val != 0) packed |= (1u << 1);
    if (readArenaSignal(arena, "VCU_MTR_FAULT_DETECTED", val) && val != 0) packed |= (1u << 2);
    if (readArenaSignal(arena, "VCU_PEDALS_FAULT_DETECTED", val) && val != 0) packed |= (1u << 3);
    if (readArenaSignal(arena, "VCU_STEERING_FAULT_DETECTED", val) && val != 0) packed |= (1u << 4);
    state.vcuFaultCode = packed;
  }
  if (readArenaSignal(arena, "Motor_Ready", val)) state.motorReadyToDrive = (val != 0);
  if (readArenaSignal(arena, "VCU_Pedals_Watchdog", val)) state.vcuPedalsOK = (val == 0);
  if (readArenaSignal(arena, "VCU_Driver_Input_Watchdog", val)) state.vcuDriverInputOK = (val == 0);
  if (readArenaSignal(arena, "VCU_Regen_Active", val)) state.vcuRegenActive = (val != 0);
  if (readArenaSignal(arena, "VCU_Regen_OK", val)) state.vcuRegenOK = (val != 0);
  if (readArenaSignal(arena, "VCU_Precharge_Motor_Voltage", val)) state.prechargeMotorV = (float)val;

  if (readArenaSignal(arena, "MPPT_Input_Voltage", val)) state.mppt[0].vin = (float)val;
  if (readArenaSignal(arena, "MPPT_Input_Current", val)) state.mppt[0].iin = (float)val;
  if (readArenaSignal(arena, "MPPT_Output_Voltage", val)) state.mppt[0].vout = (float)val;
  if (readArenaSignal(arena, "MPPT_Output_Current", val)) state.mppt[0].iout = (float)val;
  if (readArenaSignal(arena, "MPPT_HeatsinkTemperature", val)) state.mppt[0].heatsinkTemp = (float)val;
  if (readArenaSignal(arena, "MPPT_AmbientTemperature", val)) state.mppt[0].ambientTemp = (float)val;
  if (readArenaSignal(arena, "MPPT_Fault", val)) state.mppt[0].fault = (uint8_t)val;
  if (readArenaSignal(arena, "MPPT_Mode", val)) state.mppt[0].mode = (uint8_t)val;
  if (readArenaSignal(arena, "MPPT_Enabled", val)) state.mppt[0].enabled = (val != 0);

  if (readArenaSignal(arena, "Coolant_Temperature_1", val)) state.coolantTemp1 = (float)val;
  if (readArenaSignal(arena, "Coolant_Temperature_2", val)) state.coolantTemp2 = (float)val;
  if (readArenaSignal(arena, "FlowRate_1", val)) state.flowRate1 = (float)val;
  if (readArenaSignal(arena, "FlowRate_2", val)) state.flowRate2 = (float)val;
  if (readArenaSignal(arena, "Pump_DutyCycle", val)) state.pumpDuty = (uint8_t)val;
  if (readArenaSignal(arena, "Pump_Fault", val)) state.pumpFault = (val != 0);

  if (readArenaSignal(arena, "LWS_Angle", val)) state.steeringAngle = (float)val;
  if (readArenaSignal(arena, "LWS_Fault", val)) state.steeringSensorOK = (val == 0);

  if (readFirstArenaSignal(arena, {"Supplemental_Charging_Status", "SuppCharger_Status"}, val)) state.suppChargerStatus = (uint8_t)val;
  if (readFirstArenaSignal(arena, {"Supplemental_Vicor_Voltage", "Supplemental_DCDC_Voltage"}, val)) state.suppDcdcVoltage = (float)val;
  if (readArenaSignal(arena, "Supplemental_Vicor_Current", val)) state.suppDcdcCurrent = (float)val;
  else if (readArenaSignal(arena, "Supplemental_DCDC_Current", val)) state.suppDcdcCurrent = milliampsToAmps(val);

  if (readArenaSignal(arena, "AccelPedal_Main_Pos", val)) state.accelPosMain = (uint8_t)val;
  if (readArenaSignal(arena, "AccelPedal_Redundant_Pos", val)) state.accelPosRedundant = (uint8_t)val;
  if (readArenaSignal(arena, "BrakePedal_Main_Pos", val)) state.brakePosMain = (uint8_t)val;
  if (readArenaSignal(arena, "BrakePedal_Redundant_Pos", val)) state.brakePosRedundant = (uint8_t)val;
  if (readArenaSignal(arena, "AccelPedal_Main_Fault", val)) state.accelMainFault = (val != 0);
  if (readArenaSignal(arena, "AccelPedal_Redundant_Fault", val)) state.accelRedundantFault = (val != 0);
  if (readArenaSignal(arena, "BrakePedal_Main_Fault", val)) state.brakeMainFault = (val != 0);
  if (readArenaSignal(arena, "BrakePedal_Redundant_Fault", val)) state.brakeRedundantFault = (val != 0);
  if (readArenaSignal(arena, "Brake_Pressure_1_Fault", val)) state.brakePressure1Fault = (val != 0);
  if (readArenaSignal(arena, "Brake_Pressure_2_Fault", val)) state.brakePressure2Fault = (val != 0);

  if (readArenaSignal(arena, "Accel_Pos_Voltage_Main", val)) state.accelVoltMain = (float)val;
  if (readArenaSignal(arena, "Accel_Pos_Voltage_Redundant", val)) state.accelVoltRedundant = (float)val;
  if (readArenaSignal(arena, "Brake_Pos_Voltage_Main", val)) state.brakeVoltMain = (float)val;
  if (readArenaSignal(arena, "Brake_Pos_Voltage_Redundant", val)) state.brakeVoltRedundant = (float)val;

  {
    bool hasBrakePressure1 = readFirstArenaSignal(arena, {"Brake_Pressure_1", "Brake_Pressure_1.Brake_Pressure"}, val);
    if (hasBrakePressure1) state.brakePressure1 = (float)val;
    bool hasBrakePressure2 = readFirstArenaSignal(arena, {"Brake_Pressure_2", "Brake_Pressure_2.Brake_Pressure"}, val);
    if (hasBrakePressure2) state.brakePressure2 = (float)val;

    if (hasBrakePressure1 && hasBrakePressure2) {
      state.brakePressure = (state.brakePressure1 + state.brakePressure2) * 0.5f;
    } else if (hasBrakePressure1) {
      state.brakePressure = state.brakePressure1;
    } else if (hasBrakePressure2) {
      state.brakePressure = state.brakePressure2;
    } else if (readArenaSignal(arena, "Brake_Pressure", val)) {
      state.brakePressure = (float)val;
    }
    if (state.brakePressure > 10.0f) state.brakeEngaged = true;
  }
  if (readArenaSignal(arena, "Brake_Pressure_1_Voltage", val)) state.brakePressure1V = (float)val;
  if (readArenaSignal(arena, "Brake_Pressure_2_Voltage", val)) state.brakePressure2V = (float)val;

  if (readArenaSignal(arena, "Horn_Pressed", val)) state.hornPressed = (val != 0);
  if (readArenaSignal(arena, "Hazard_Pressed", val)) state.hazardPressed = (val != 0);
  if (readArenaSignal(arena, "PushToTalk_Pressed", val)) state.pttPressed = (val != 0);
  if (readArenaSignal(arena, "Cruise_Set", val)) state.cruiseSet = (val != 0);
  if (readArenaSignal(arena, "Regen_Activate", val)) state.regenActivate = (val != 0);

  if (readArenaSignal(arena, "LTC4421_HVDCDC_Selected", val)) state.lvHvDcdcSelected = (val != 0);
  if (readArenaSignal(arena, "LTC4421_HVDCDC_Fault", val)) state.lvHvDcdcFault = (val != 0);
  if (readArenaSignal(arena, "LTC4421_HVDCDC_Valid", val)) state.lvHvDcdcValid = (val != 0);
  if (readArenaSignal(arena, "LTC4421_SuppBatt_Selected", val)) state.lvSuppBattSelected = (val != 0);
  if (readArenaSignal(arena, "LTC4421_SuppBatt_Fault", val)) state.lvSuppBattFault = (val != 0);
  if (readArenaSignal(arena, "LTC4421_SuppBatt_Valid", val)) state.lvSuppBattValid = (val != 0);
  if (readArenaSignal(arena, "LV_EN_SupplementalBattery", val)) state.lvEnSuppBattery = (val != 0);
  if (readArenaSignal(arena, "LV_EN_PowerSupply", val)) state.lvEnPowerSupply = (val != 0);

  if (readArenaSignal(arena, "Camera_Status_Backup", val)) state.cameraBackup = (val != 0);
  if (readArenaSignal(arena, "Camera_Status_Left", val)) state.cameraLeft = (val != 0);
  if (readArenaSignal(arena, "Camera_Status_Right", val)) state.cameraRight = (val != 0);
  if (readArenaSignal(arena, "Display_FrameRate", val)) state.displayFps = (uint8_t)val;

  if (readArenaSignal(arena, "Controls_Lighting_Fault", val)) state.lightingFaults = (uint8_t)val;
  if (readArenaSignal(arena, "Controls_Leader_Fault", val)) state.controlsLeaderFault = (uint8_t)val;

  if (envFlagEnabled("PHOTON_FAKE_CAN_FAULTS")) {
    applyFakeCanFaults(state);
  }
}

}  // namespace ui
