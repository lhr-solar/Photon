#include "dbc.hpp"
#include "imgui.h"
#include "../network/network.hpp"

#include <cstdint>

namespace {

uint64_t extractBitsLe(uint64_t value, uint8_t startBit, uint8_t bitCount){
    if (bitCount == 0 || startBit >= 64) { return 0; }
    if (bitCount >= 64) { return value; }
    const uint64_t mask = (bitCount == 64) ? ~0ULL : ((1ULL << bitCount) - 1ULL);
    return (value >> startBit) & mask;
}

int32_t signExtend(uint64_t raw, uint8_t bits){
    const uint64_t signBit = 1ULL << (bits - 1);
    const uint64_t mask = (1ULL << bits) - 1ULL;
    uint64_t v = raw & mask;
    if (v & signBit) {
        v |= ~mask;
    }
    return static_cast<int32_t>(v);
}

}  // namespace

void IO_State::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    double maxTime = 5.0;

    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);

    Acceleration_Percentage.push_back(static_cast<int>(extractBitsLe(encoded, 0, 8)));
    Brake_Percentage.push_back(static_cast<int>(extractBitsLe(encoded, 8, 8)));
    const uint64_t flags = extractBitsLe(encoded, 16, 8);
    IGN_Array.push_back(flags & 0x01);
    IGN_Motor.push_back((flags >> 1) & 0x01);
    Regen_SW.push_back((flags >> 2) & 0x01);
    Forward_Gear.push_back((flags >> 3) & 0x01);
    Reverse_Gear.push_back((flags >> 4) & 0x01);
    Cruz_EN.push_back((flags >> 5) & 0x01);
    Cruz_Set.push_back((flags >> 6) & 0x01);
    Brake_Light.push_back((flags >> 7) & 0x01);
}

/*
void Controls_Fault::updateSignals(std::uint64_t encoded){
    const uint64_t flags = extractBitsLe(encoded, 0, 8);
    Controls_Fault_Flag = flags & 0x01;
    Motor_Controller_Fault = (flags >> 1) & 0x01;
    BPS_Fault = (flags >> 2) & 0x01;
    Pedals_Fault = (flags >> 3) & 0x01;
    CarCAN_Fault = (flags >> 4) & 0x01;
    Internal_Controls_Fault = (flags >> 5) & 0x01;
    OS_Fault = (flags >> 6) & 0x01;
    Lakshay_Fault = (flags >> 7) & 0x01;
}

void Motor_Controller_Safe::updateSignals(std::uint64_t encoded){
    const uint64_t flags = extractBitsLe(encoded, 0, 8);
    Motor_Safe = flags & 0x01;
    Motor_Controller_Error = (flags >> 1) & 0x01;
}

void Motor_Drive_Command::updateSignals(std::uint64_t encoded){
    Motor_Velocity_Setpoint = static_cast<std::int32_t>(extractBitsLe(encoded, 0, 32));
    Motor_Current_Setpoint = static_cast<std::int32_t>(extractBitsLe(encoded, 32, 32));
}

void Motor_Power_Command::updateSignals(std::uint64_t encoded){
    Motor_Power_Setpoint = static_cast<std::int32_t>(extractBitsLe(encoded, 32, 32));
}

void Pedals_Raw_Voltage::updateSignals(std::uint64_t encoded){
    Brake_Raw = signExtend(extractBitsLe(encoded, 0, 15), 15);
    Accel_Raw = signExtend(extractBitsLe(encoded, 16, 15), 15);
}
*/
