#pragma once
#include <cstdint>
#include <chrono>
#include <vector>
#include "../gui/plot.hpp"
#include "network.hpp"

// Simple PODs mirroring assets/dbc/controls.dbc. Each struct exposes an
// updateSignals() helper that decodes a pre-parsed 64-bit payload for its CAN ID.

// Controls
// at a high level, every CAN DBC entry has
struct CanSignal {
    int startBit = 0;
    int length = 0;
    int endianness = 0;
    bool isSigned = false;
    double scale = 1.0;
    double offset = 0.0;
    double min = 0.0;
    double max = 0.0;
    std::string name;
    std::string unit;
    std::string receiver;
    std::chrono::system_clock::time_point lastTimeMutated;
    std::chrono::milliseconds timeSinceMutation;
};

struct CanMessage{
    int canId = 0;
    int dlc = 0;
    std::string name;
    std::string transmitter;
    std::chrono::system_clock::time_point lastTimeUpdated;
    std::chrono::milliseconds timeSinceUpdate;
    double dataRate;
    double storageSize; // sum of all 
    double bandwidthPercentage; // percentage of total data sent
    std::vector<double> time {0.0};
    std::vector<CanSignal> signals;
    void updateMessage(Network* networkSource);
};

struct CanStore{
    std::map<int,CanMessage> canMessages;
    double totalBandwidth; // total data stored
    bool loadStateFromFile(std::string filePath);
    void dump();
};

struct IO_State{
    static constexpr uint32_t kId = 0x581;
    std::vector<double> time {0.0};
    std::vector<double> Acceleration_Percentage {0};
    std::vector<double> Brake_Percentage {0};
    std::vector<double> IGN_Array {0};
    std::vector<double> IGN_Motor {0};
    std::vector<double> Regen_SW {0};
    std::vector<double> Forward_Gear {0};
    std::vector<double> Reverse_Gear {0};
    std::vector<double> Cruz_EN {0};
    std::vector<double> Cruz_Set {0};
    std::vector<double> Brake_Light {0};
    void updateSignals(Network* networkSource);
};

struct Controls_Fault{
    static constexpr uint32_t kId = 0x583;
    std::vector<double> time {0.0};
    std::vector<double> Controls_Fault_Flag {0};
    std::vector<double> Motor_Controller_Fault {0};
    std::vector<double> BPS_Fault {0};
    std::vector<double> Pedals_Fault {0};
    std::vector<double> CarCAN_Fault {0};
    std::vector<double> Internal_Controls_Fault {0};
    std::vector<double> OS_Fault {0};
    std::vector<double> Lakshay_Fault {0};

    void updateSignals(Network* networkSource);
};

struct Motor_Controller_Safe{
    static constexpr uint32_t kId = 0x584;
    std::vector<double> time {0.0};
    std::vector<double> Motor_Safe {0};
    std::vector<double> Motor_Controller_Error {0};

    void updateSignals(Network* networkSource);
};

struct Motor_Drive_Command{
    static constexpr uint32_t kId = 0x221;
    std::vector<double> time {0.0};
    std::vector<double> Motor_Current_Setpoint {0};
    std::vector<double> Motor_Velocity_Setpoint {0};

    void updateSignals(Network* networkSource);
};

struct Motor_Power_Command{
    static constexpr uint32_t kId = 0x222;
    std::vector<double> time {0.0};
    std::vector<double> Motor_Power_Setpoint {0};

    void updateSignals(Network* networkSource);
};

struct Pedals_Raw_Voltage{
    static constexpr uint32_t kId = 0x585;
    std::vector<double> time {0.0};
    std::vector<double> Brake_Raw {0};
    std::vector<double> Accel_Raw {0};

    void updateSignals(Network* networkSource);
};

// BPS
struct BPS_Trip{
    static constexpr uint32_t kId = 0x002;
    std::vector<double> time {0.0};
    std::vector<double> BPS_Trip_Flag {0};
    void updateSignals(Network* networkSource);
};

struct BPS_All_Clear{
    static constexpr uint32_t kId = 0x101;
    std::vector<double> time {0.0};
    std::vector<double> BPS_All_Clear_Flag {0};
    void updateSignals(Network* networkSource);
};

struct BPS_Contactor_State{
    static constexpr uint32_t kId = 0x102;
    std::vector<double> time {0.0};
    std::vector<double> Array_Contactor {0};
    std::vector<double> HV_Contactor {0};
    void updateSignals(Network* networkSource);
};

struct BPS_Current{
    static constexpr uint32_t kId = 0x103;
    std::vector<double> time {0.0};
    std::vector<double> Current_mA {0};
    void updateSignals(Network* networkSource);
};

struct BPS_Voltage_Array{
    static constexpr uint32_t kId = 0x104;
    std::vector<double> time {0.0};
    std::vector<double> Voltage_idx {0};
    std::vector<double> Voltage_Value_mV {0};
    void updateSignals(Network* networkSource);
};

struct BPS_Temperature_Array{
    static constexpr uint32_t kId = 0x105;
    std::vector<double> time {0.0};
    std::vector<double> Temperature_idx {0};
    std::vector<double> Temperature_Value_mC {0};
    void updateSignals(Network* networkSource);
};

struct BPS_SOC{
    static constexpr uint32_t kId = 0x106;
    std::vector<double> time {0.0};
    std::vector<double> SoC_percent {0};
    void updateSignals(Network* networkSource);
};

struct BPS_WDog_Trigger{
    static constexpr uint32_t kId = 0x107;
    std::vector<double> time {0.0};
    std::vector<double> WDog_Trig {0};
    void updateSignals(Network* networkSource);
};

struct BPS_CAN_Error{
    static constexpr uint32_t kId = 0x108;
    std::vector<double> time {0.0};
    std::vector<double> BPS_CAN_Error_Flag {0};
    void updateSignals(Network* networkSource);
};

struct BPS_Command{
    static constexpr uint32_t kId = 0x109;
    std::vector<double> time {0.0};
    std::vector<double> BPS_Command_Value {0};
    void updateSignals(Network* networkSource);
};

struct BPS_Supplemental_Voltage{
    static constexpr uint32_t kId = 0x10B;
    std::vector<double> time {0.0};
    std::vector<double> Supplemental_Voltage_mV {0};
    void updateSignals(Network* networkSource);
};

struct BPS_Charge_Enabled{
    static constexpr uint32_t kId = 0x10C;
    std::vector<double> time {0.0};
    std::vector<double> Charge_Enabled {0};
    void updateSignals(Network* networkSource);
};

struct BPS_Voltage_Summary{
    static constexpr uint32_t kId = 0x10D;
    std::vector<double> time {0.0};
    std::vector<double> Pack_Voltage_mV {0};
    std::vector<double> Voltage_Range_mV {0};
    std::vector<double> Voltage_Timestamp_ms {0};
    void updateSignals(Network* networkSource);
};

struct BPS_Temperature_Summary{
    static constexpr uint32_t kId = 0x10E;
    std::vector<double> time {0.0};
    std::vector<double> Average_Temp_mC {0};
    std::vector<double> Temperature_Range_mC {0};
    std::vector<double> Temperature_Timestamp_ms {0};
    void updateSignals(Network* networkSource);
};

struct BPS_Fault_State{
    static constexpr uint32_t kId = 0x10F;
    std::vector<double> time {0.0};
    std::vector<double> BPS_Fault_State_Value {0};
    void updateSignals(Network* networkSource);
};
