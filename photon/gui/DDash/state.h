#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ui {

/**
 * Fault severity levels
 */
enum class FaultSeverity {
    Info,
    Warning,
    Critical
};

/**
 * Gear positions: Forward / Neutral / Reverse
 */
enum class Gear {
    Forward,
    Neutral,
    Reverse
};

/**
 * Turn signal states
 */
enum class TurnSignal {
    None,
    Left,
    Right
};

/**
 * Individual fault record
 */
struct Fault {
    std::string code;
    std::string message;
    FaultSeverity severity;
    int64_t timestamp;  // Unix timestamp in milliseconds
};

/**
 * Main battery state
 */
struct MainBattery {
    float soc;      // State of charge (0-100)
    float voltage;  // Voltage in V
    float current;  // Current in A (negative = discharging)
};

/**
 * Supplementary (auxiliary) battery state
 */
struct SuppBattery {
    float soc;      // State of charge (0-100)
    float voltage;  // Voltage in V
    float current;  // Current in A
};

/**
 * Motor controller (inverter) telemetry
 */
struct MotorController {
    float heatsinkTemp;  // Heatsink temperature in C
    float voltage;       // Bus voltage in V
    float current;       // Bus current in A
};

/**
 * Cruise control state
 */
struct CruiseControl {
    bool enabled;
    int setSpeed;  // Target speed in km/h
};

/**
 * Contactor states for high voltage system
 * 6 contactors: HV+, HV-, Array Precharge, Array, Motor Precharge, Motor
 */
struct ContactorStates {
    bool hvPositive;        // HP - HV+ contactor
    bool hvNegative;        // HN - HV- contactor
    bool arrayPrecharge;    // AP - Array precharge contactor
    bool arrayContactor;    // AN - Array contactor
    bool motorPrecharge;    // MP - Motor precharge contactor
    bool motorContactor;    // MM - Motor contactor
};

/**
 * Ignition / enable states
 * 3 enable switches: Low Voltage, Array, Motor
 */
struct IgnitionStates {
    bool lvEnabled;         // LV - Low Voltage enable
    bool arrayEnabled;      // A  - Array enable
    bool motorEnabled;      // M  - Motor enable
};

/**
 * Complete application state
 * All widgets read/write from this struct - no globals.
 */
struct AppState {
    int speed;              // Current speed in km/h
    Gear gear;
    MainBattery mainBattery;
    SuppBattery suppBattery;
    MotorController motorController;
    CruiseControl cruise;
    bool brakeEngaged;
    bool regenEnabled;
    ContactorStates contactorStates;
    IgnitionStates ignitionStates;
    bool simulationEnabled = false;
    uint8_t heartbeat;      // 0-255 cycling heartbeat counter
    std::vector<Fault> faults;
    TurnSignal turnSignal;

    // Unrecoverable CAN fault
    bool canFault = false;
    uint16_t canFaultId = 0;
    std::string canFaultName;
    std::string canFaultMessage;

    // Recoverable CAN fault
    bool canFaultRecoverable = false;
    uint16_t canFaultRecoverableId = 0;
    std::string canFaultRecoverableName;
    std::string canFaultRecoverableMessage;

    // Camera texture IDs (nullptr = show placeholder)
    void* leftCameraTexture  = nullptr;
    void* rightCameraTexture = nullptr;
    void* rearCameraTexture  = nullptr;
    
    // Additional telemetry
    float pedalPercent = 0.0f;
    float brakePressure = 0.0f;
    
    // BPS Module summaries
    std::vector<float> moduleVoltages;
    std::vector<float> moduleTemps;
    
    // UI state
    bool showDebugScreen = false;
};

/**
 * Initialize AppState with default values
 */
inline AppState CreateDefaultState() {
    AppState state{};
    state.speed = 0;
    state.gear = Gear::Neutral;
    state.mainBattery = { 60.0f, 120.0f, -60.0f };
    state.suppBattery = { 80.0f, 24.0f, 0.0f };
    state.motorController = { 42.0f, 120.0f, -60.0f };
    state.cruise = { false, 0 };
    state.brakeEngaged = false;
    state.regenEnabled = false;
    state.contactorStates = { false, false, false, false, false, false };
    state.ignitionStates = { false, false, false };
    state.heartbeat = 0;
    state.turnSignal = TurnSignal::None;
    state.canFault = false;
    state.canFaultId = 0;
    state.canFaultName.clear();
    state.canFaultMessage.clear();
    state.canFaultRecoverable = false;
    state.canFaultRecoverableId = 0;
    state.canFaultRecoverableName.clear();
    state.canFaultRecoverableMessage.clear();
    return state;
}

/**
 * Helper to convert Gear enum to display string (short)
 */
inline const char* GearToString(Gear gear) {
    switch (gear) {
        case Gear::Forward: return "F";
        case Gear::Neutral: return "N";
        case Gear::Reverse: return "R";
        default:            return "?";
    }
}

/**
 * Helper to convert Gear enum to long display string
 */
inline const char* GearToLongString(Gear gear) {
    switch (gear) {
        case Gear::Forward: return "F - Forward";
        case Gear::Neutral: return "N - Neutral";
        case Gear::Reverse: return "R - Reverse";
        default:            return "?";
    }
}

/**
 * Helper to get Gear from index (for iteration)
 */
inline Gear GearFromIndex(int index) {
    switch (index) {
        case 0: return Gear::Forward;
        case 1: return Gear::Neutral;
        case 2: return Gear::Reverse;
        default: return Gear::Neutral;
    }
}

} // namespace ui
