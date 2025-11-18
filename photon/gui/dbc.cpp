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

void Controls_Fault::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);

    const uint64_t flags = extractBitsLe(encoded, 0, 8);
    Controls_Fault_Flag.push_back(flags & 0x01);
    Motor_Controller_Fault.push_back((flags >> 1) & 0x01);
    BPS_Fault.push_back((flags >> 2) & 0x01);
    Pedals_Fault.push_back((flags >> 3) & 0x01);
    CarCAN_Fault.push_back((flags >> 4) & 0x01);
    Internal_Controls_Fault.push_back((flags >> 5) & 0x01);
    OS_Fault.push_back((flags >> 6) & 0x01);
    Lakshay_Fault.push_back((flags >> 7) & 0x01);
}

void Motor_Controller_Safe::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);

    const uint64_t flags = extractBitsLe(encoded, 0, 8);
    Motor_Safe.push_back(flags & 0x01);
    Motor_Controller_Error.push_back((flags >> 1) & 0x01);
}

void Motor_Drive_Command::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);

    Motor_Velocity_Setpoint.push_back(static_cast<std::int32_t>(extractBitsLe(encoded, 0, 32)));
    Motor_Current_Setpoint.push_back(static_cast<std::int32_t>(extractBitsLe(encoded, 32, 32)));
}

void Motor_Power_Command::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);

    Motor_Power_Setpoint.push_back(static_cast<std::int32_t>(extractBitsLe(encoded, 32, 32)));
}

void Pedals_Raw_Voltage::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);

    Brake_Raw.push_back(signExtend(extractBitsLe(encoded, 0, 15), 15));
    Accel_Raw.push_back(signExtend(extractBitsLe(encoded, 16, 15), 15));
}

void BPS_Trip::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    BPS_Trip_Flag.push_back(extractBitsLe(encoded, 0, 1));
}

void BPS_All_Clear::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    BPS_All_Clear_Flag.push_back(extractBitsLe(encoded, 0, 1));
}

void BPS_Contactor_State::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    Array_Contactor.push_back(extractBitsLe(encoded, 0, 1));
    HV_Contactor.push_back(extractBitsLe(encoded, 2, 1));
}

void BPS_Current::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    Current_mA.push_back(signExtend(extractBitsLe(encoded, 0, 32), 32));
}

void BPS_Voltage_Array::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    // Producer builds the 40-bit payload big-endian: first byte is index, next 4 bytes are value (mV)
    const uint64_t idx = (encoded >> 32) & 0xFF;
    const uint64_t value = encoded & 0xFFFFFFFFULL;
    Voltage_idx.push_back(static_cast<double>(idx));
    Voltage_Value_mV.push_back(static_cast<double>(value));
}

void BPS_Temperature_Array::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    const uint64_t idx = (encoded >> 40) & 0xFF; // first data byte is the index
    const uint64_t b1 = (encoded >> 32) & 0xFF;
    const uint64_t b2 = (encoded >> 24) & 0xFF;
    const uint64_t b3 = (encoded >> 16) & 0xFF;
    const uint64_t b4 = (encoded >> 8)  & 0xFF;
    const uint64_t b5 = encoded & 0xFF;
    const uint64_t value = b1 | (b2 << 8) | (b3 << 16) | (b4 << 24) | (b5 << 32); // little-endian payload (mC)
    Temperature_idx.push_back(static_cast<double>(idx));
    Temperature_Value_mC.push_back(static_cast<double>(value));
}

void BPS_SOC::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    SoC_percent.push_back(extractBitsLe(encoded, 0, 32));
}

void BPS_WDog_Trigger::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    WDog_Trig.push_back(extractBitsLe(encoded, 0, 1));
}

void BPS_CAN_Error::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    BPS_CAN_Error_Flag.push_back(extractBitsLe(encoded, 0, 1));
}

void BPS_Command::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    BPS_Command_Value.push_back(extractBitsLe(encoded, 0, 32));
}

void BPS_Supplemental_Voltage::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    Supplemental_Voltage_mV.push_back(extractBitsLe(encoded, 0, 16));
}

void BPS_Charge_Enabled::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    Charge_Enabled.push_back(extractBitsLe(encoded, 0, 1));
}

void BPS_Voltage_Summary::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    Pack_Voltage_mV.push_back(extractBitsLe(encoded, 0, 24));
    Voltage_Range_mV.push_back(extractBitsLe(encoded, 24, 24));
    Voltage_Timestamp_ms.push_back(extractBitsLe(encoded, 48, 16));
}

void BPS_Temperature_Summary::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    Average_Temp_mC.push_back(extractBitsLe(encoded, 0, 24));
    Temperature_Range_mC.push_back(extractBitsLe(encoded, 24, 24));
    Temperature_Timestamp_ms.push_back(extractBitsLe(encoded, 48, 16));
}

void BPS_Fault_State::updateSignals(Network* networkSource){
    uint64_t encoded = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    networkSource->readSample(kId, encoded);
    time.push_back(time.back() + deltaTime);
    BPS_Fault_State_Value.push_back(extractBitsLe(encoded, 0, 8));
}
