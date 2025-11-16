#pragma once
#include <cstdint>
#include "plot.hpp"
#include "../network/network.hpp"

// Simple PODs mirroring assets/dbc/controls.dbc. Each struct exposes an
// updateSignals() helper that decodes a pre-parsed 64-bit payload for its CAN ID.

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

/*
struct Controls_Fault{
    static constexpr uint32_t kId = 0x583;
    int Controls_Fault_Flag = 0;
    int Motor_Controller_Fault = 0;
    int BPS_Fault = 0;
    int Pedals_Fault = 0;
    int CarCAN_Fault = 0;
    int Internal_Controls_Fault = 0;
    int OS_Fault = 0;
    int Lakshay_Fault = 0;

    void updateSignals(std::uint64_t encoded);
    Plot plot;
    Controls_Fault():plot(kId, "Controls_Fault", "Controls_Fault"){}
};

struct Motor_Controller_Safe{
    static constexpr uint32_t kId = 0x584;
    int Motor_Safe = 0;
    int Motor_Controller_Error = 0;

    void updateSignals(std::uint64_t encoded);
    Plot plot;
    Motor_Controller_Safe():plot(kId, "Motor_Controller_Safe", "Motor_Controller_Safe"){}
};

struct Motor_Drive_Command{
    static constexpr uint32_t kId = 0x221;
    std::int32_t Motor_Current_Setpoint = 0;
    std::int32_t Motor_Velocity_Setpoint = 0;

    void updateSignals(std::uint64_t encoded);
    Plot plotCurrent;
    Plot plotVelocity;
    Motor_Drive_Command()
        : plotCurrent(kId, "Motor_Current_Setpoint", "Motor_Current_Setpoint"),
          plotVelocity(kId, "Motor_Velocity_Setpoint", "Motor_Velocity_Setpoint"){}
};

struct Motor_Power_Command{
    static constexpr uint32_t kId = 0x222;
    std::int32_t Motor_Power_Setpoint = 0;

    void updateSignals(std::uint64_t encoded);
    Plot plot;
    Motor_Power_Command():plot(kId, "Motor_Power_Setpoint", "Motor_Power_Setpoint"){}
};

struct Pedals_Raw_Voltage{
    static constexpr uint32_t kId = 0x585;
    std::int32_t Brake_Raw = 0;
    std::int32_t Accel_Raw = 0;

    void updateSignals(std::uint64_t encoded);
    Plot plotBrake;
    Plot plotAccel;
    Pedals_Raw_Voltage()
        : plotBrake(kId, "Brake_Raw", "Brake_Raw"),
          plotAccel(kId, "Accel_Raw", "Accel_Raw"){}
};
*/
