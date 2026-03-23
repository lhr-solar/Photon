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

    // Extra telemetry
    float phaseCurrentB;
    float phaseCurrentC;
    float backEmfQ;
    float backEmfD;

    // Status limits
    bool limitOutputVoltage;
    bool limitMotorCurrent;
    bool limitVelocity;
    bool limitBusCurrent;
    bool limitBusVoltageUpper;
    bool limitBusVoltageLower;
    bool limitIpmOrMotorTemp;
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
    
    // BPS status signals (from BPSCAN)
    float mainBatteryAvgTemp = 0.0f;    // Average pack temperature
    bool bpsRegenOK = false;            // Regen allowed
    bool bpsChargeOK = false;           // Charging allowed
    float prechargeBatteryV = 0.0f;     // Precharge circuit battery voltage
    float prechargeArrayV = 0.0f;       // Precharge circuit array voltage
    float mainBatteryCurrentRawV = 0.0f;// Raw current sensor voltage
    uint8_t bpsFaultCode = 0;           // BPS fault value (0=no fault, see table)
    
    // VCU status (CarCAN ID 24)
    uint8_t vcuFsmState = 0;            // VCU finite state machine state
    uint8_t vcuFaultCode = 0;           // VCU fault code
    bool motorReadyToDrive = false;
    bool vcuPedalsOK = false;
    bool vcuDriverInputOK = false;
    bool vcuRegenActive = false;
    bool vcuRegenOK = false;
    float prechargeMotorV = 0.0f;       // Motor precharge voltage (ID 33)
    
    // MPPT solar array (3 channels A/B/C)
    struct MPPTChannel {
        float vin = 0, iin = 0, vout = 0, iout = 0;
        float heatsinkTemp = 0, ambientTemp = 0;
        uint8_t fault = 0, mode = 0;
        bool enabled = false;
    };
    MPPTChannel mppt[3]; // A=0, B=1, C=2
    
    // Cooling system
    float coolantTemp1 = 0.0f, coolantTemp2 = 0.0f;
    float flowRate1 = 0.0f, flowRate2 = 0.0f;
    uint8_t pumpDuty = 0;
    bool pumpFault = false;
    
    // Steering wheel sensor
    float steeringAngle = 0.0f;         // degrees
    bool steeringSensorOK = false;
    
    // Supp battery charger
    uint8_t suppChargerStatus = 0;      // 0=disabled, 1=done, 2=charging, 3=fault
    float suppDcdcVoltage = 0.0f;
    float suppDcdcCurrent = 0.0f;
    
    // Pedal sensor details (ID 80/81)
    uint8_t accelPosMain = 0, accelPosRedundant = 0;
    uint8_t brakePosMain = 0, brakePosRedundant = 0;
    bool accelMainFault = false, accelRedundantFault = false;
    bool brakeMainFault = false, brakeRedundantFault = false;
    bool brakePressure1Fault = false, brakePressure2Fault = false;
    float accelVoltMain = 0, accelVoltRedundant = 0;
    float brakeVoltMain = 0, brakeVoltRedundant = 0;
    
    // Brake pressure dual sensors (ID 1616)
    float brakePressure1 = 0.0f, brakePressure2 = 0.0f;
    float brakePressure1V = 0.0f, brakePressure2V = 0.0f;
    
    // Driver input buttons (ID 96)
    bool hornPressed = false;
    bool hazardPressed = false;
    bool pttPressed = false;
    bool cruiseSet = false;
    bool regenActivate = false;
    
    // LV carrier status (ID 1536)
    bool lvHvDcdcSelected = false, lvHvDcdcFault = false, lvHvDcdcValid = false;
    bool lvSuppBattSelected = false, lvSuppBattFault = false, lvSuppBattValid = false;
    bool lvEnSuppBattery = false, lvEnPowerSupply = false;
    
    // Camera status (ID 1792)
    bool cameraBackup = false, cameraLeft = false, cameraRight = false;
    uint8_t displayFps = 0;
    
    // Lighting faults
    uint8_t lightingFaults = 0;
    uint8_t controlsLeaderFault = 0;
    
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
    state.mainBattery = { 0.0f, 0.0f, 0.0f };
    state.suppBattery = { 0.0f, 0.0f, 0.0f };
    state.motorController = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false, false, false, false, false, false };
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
