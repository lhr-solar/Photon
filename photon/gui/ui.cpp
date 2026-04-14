#include "ui.hpp"
#include "../engine/include.hpp"
#include "DDash/ui.h"
#include <chrono>
#include <cmath>
#include <algorithm>

void UI::build(){
    ImGui::NewFrame();

    static ui::AppState ddashState = ui::CreateDefaultState();
    if (ddashState.simulationEnabled) {
        ui::UpdateSimulation(ddashState, ImGui::GetIO().DeltaTime);
    } else if (networkINTF) {
        double val = 0;
        // Motor controller basics — McQueen DBCs prefix every MC signal with "MC_".
        if (networkINTF->readParsedSignal("MC_VehicleVelocity", val)) ddashState.speed = (int)(val * 3.6);
        if (networkINTF->readParsedSignal("Main_Battery_Voltage", val)) ddashState.mainBattery.voltage = (float)val;
        if (networkINTF->readParsedSignal("Main_Battery_Current", val)) ddashState.mainBattery.current = (float)val;
        if (networkINTF->readParsedSignal("Supplemental_Battery_Voltage", val)) ddashState.suppBattery.voltage = (float)val;
        if (networkINTF->readParsedSignal("MC_HeatsinkTemp", val)) ddashState.motorController.heatsinkTemp = (float)val;
        if (networkINTF->readParsedSignal("MC_BusVoltage", val)) ddashState.motorController.voltage = (float)val;
        if (networkINTF->readParsedSignal("MC_BusCurrent", val)) ddashState.motorController.current = (float)val;

        // Motor Controller extra telemetry
        if (networkINTF->readParsedSignal("MC_PhaseCurrentB", val)) ddashState.motorController.phaseCurrentB = (float)val;
        if (networkINTF->readParsedSignal("MC_PhaseCurrentC", val)) ddashState.motorController.phaseCurrentC = (float)val;
        if (networkINTF->readParsedSignal("MC_BEMFq", val)) ddashState.motorController.backEmfQ = (float)val;
        if (networkINTF->readParsedSignal("MC_BEMFd", val)) ddashState.motorController.backEmfD = (float)val;

        // Motor Controller limits — McQueen DBC names them MC_LIMIT_*.
        if (networkINTF->readParsedSignal("MC_LIMIT_OutputVoltagePWM", val)) ddashState.motorController.limitOutputVoltage = (val != 0);
        if (networkINTF->readParsedSignal("MC_LIMIT_MotorCurrent", val)) ddashState.motorController.limitMotorCurrent = (val != 0);
        if (networkINTF->readParsedSignal("MC_LIMIT_Velocity", val)) ddashState.motorController.limitVelocity = (val != 0);
        if (networkINTF->readParsedSignal("MC_LIMIT_BusCurrent", val)) ddashState.motorController.limitBusCurrent = (val != 0);
        if (networkINTF->readParsedSignal("MC_LIMIT_BusVoltageUpper", val)) ddashState.motorController.limitBusVoltageUpper = (val != 0);
        if (networkINTF->readParsedSignal("MC_LIMIT_BusVoltageLower", val)) ddashState.motorController.limitBusVoltageLower = (val != 0);
        if (networkINTF->readParsedSignal("MC_LIMIT_MotorTemp", val)) ddashState.motorController.limitIpmOrMotorTemp = (val != 0);
        
        // Overall CAN-fault gate. The McQueen VCU_Status no longer has a single
        // VCU_Fault byte — it has individual VCU_*_FAULT_DETECTED bits. OR the
        // ones that represent an unrecoverable or pipeline fault into a single
        // "VCU faulted" signal.
        double bpsF = 0;
        double vcuFaults[5] = {0};
        bool hasBps = networkINTF->readParsedSignal("BPS_Fault", bpsF);
        bool hasVcu = false;
        hasVcu |= networkINTF->readParsedSignal("VCU_BPS_FAULT_DETECTED",       vcuFaults[0]);
        hasVcu |= networkINTF->readParsedSignal("VCU_CONTROLS_FAULT_DETECTED",  vcuFaults[1]);
        hasVcu |= networkINTF->readParsedSignal("VCU_MTR_FAULT_DETECTED",       vcuFaults[2]);
        hasVcu |= networkINTF->readParsedSignal("VCU_PEDALS_FAULT_DETECTED",    vcuFaults[3]);
        hasVcu |= networkINTF->readParsedSignal("VCU_STEERING_FAULT_DETECTED",  vcuFaults[4]);
        double vcuF = 0;
        for (int i = 0; i < 5; ++i) vcuF += vcuFaults[i];
        if (hasBps && bpsF != 0) {
            ddashState.canFault = true;
            ddashState.canFaultId = 1;
            ddashState.canFaultName = "BPS";
            ddashState.canFaultMessage = "BPS Fault";
        } else if (hasVcu && vcuF != 0) {
            ddashState.canFault = true;
            ddashState.canFaultId = 24; // VCU_Status message ID in McQueen CarCAN
            ddashState.canFaultName = "VCU";
            ddashState.canFaultMessage = "VCU Fault";
        } else {
            ddashState.canFault = false;
        }

        if (networkINTF->readParsedSignal("HV_Plus_Contactor_State", val)) ddashState.contactorStates.hvPositive = (val != 0);
        if (networkINTF->readParsedSignal("HV_Minus_Contactor_State", val)) ddashState.contactorStates.hvNegative = (val != 0);
        if (networkINTF->readParsedSignal("Array_Precharge_Contactor_State", val)) ddashState.contactorStates.arrayPrecharge = (val != 0);
        if (networkINTF->readParsedSignal("Array_Contactor_State", val)) ddashState.contactorStates.arrayContactor = (val != 0);
        if (networkINTF->readParsedSignal("Motor_Precharge_Contactor_State", val)) ddashState.contactorStates.motorPrecharge = (val != 0);
        if (networkINTF->readParsedSignal("Motor_Contactor_State", val)) ddashState.contactorStates.motorContactor = (val != 0);

        if (networkINTF->readParsedSignal("Ignition_Off", val)) ddashState.ignitionStates.lvEnabled = (val == 0);
        if (networkINTF->readParsedSignal("Ignition_Array", val)) ddashState.ignitionStates.arrayEnabled = (val != 0);
        if (networkINTF->readParsedSignal("Ignition_Motor", val)) ddashState.ignitionStates.motorEnabled = (val != 0);

        if (networkINTF->readParsedSignal("Gear_Forward", val) && val != 0) ddashState.gear = ui::Gear::Forward;
        if (networkINTF->readParsedSignal("Gear_Reverse", val) && val != 0) ddashState.gear = ui::Gear::Reverse;
        if (networkINTF->readParsedSignal("Gear_Neutral", val) && val != 0) ddashState.gear = ui::Gear::Neutral;

        double bL = 0, bR = 0;
        if (networkINTF->readParsedSignal("Blinker_Left", bL) && bL != 0) ddashState.turnSignal = ui::TurnSignal::Left;
        else if (networkINTF->readParsedSignal("Blinker_Right", bR) && bR != 0) ddashState.turnSignal = ui::TurnSignal::Right;
        else ddashState.turnSignal = ui::TurnSignal::None;

        if (networkINTF->readParsedSignal("Cruise_Enable", val)) ddashState.cruise.enabled = (val != 0);
        if (networkINTF->readParsedSignal("Regen_Enable", val)) ddashState.regenEnabled = (val != 0);
        if (networkINTF->readParsedSignal("BrakePedal_Main_Pos", val)) ddashState.brakeEngaged = (val > 5.0);

        // New Telemetry — accel position is AccelPedal_Main_Pos in McQueen Pedal_Status.
        if (networkINTF->readParsedSignal("AccelPedal_Main_Pos", val)) ddashState.pedalPercent = (float)val;
        // BPSCAN has Main_Battery_Current but user asked for supp battery current, mapped to Supplemental_Battery_Current
        if (networkINTF->readParsedSignal("Supplemental_Battery_Current", val)) ddashState.suppBattery.current = (float)val;
        if (networkINTF->readParsedSignal("Brake_Pressure", val)) ddashState.brakePressure = (float)val;

        // BPS arrays 
        double tapIdx = 0, tapV = 0, tapT = 0;
        if (networkINTF->readParsedSignal("BPS_Tap_idx", tapIdx)) {
            int idx = (int)tapIdx;
            if (ddashState.moduleVoltages.size() <= idx) ddashState.moduleVoltages.resize(idx + 1, 0.0f);
            if (ddashState.moduleTemps.size() <= idx) ddashState.moduleTemps.resize(idx + 1, 0.0f);
            
            if (networkINTF->readParsedSignal("BPS_Voltage_Tap_Data", tapV)) {
                ddashState.moduleVoltages[idx] = (float)tapV;
            }
            if (networkINTF->readParsedSignal("BPS_Temperature_Tap_Data", tapT)) {
                ddashState.moduleTemps[idx] = (float)tapT;
            }
        }

        // BPS status signals (from BPSCAN)
        if (networkINTF->readParsedSignal("Main_Battery_Avg_Temperature", val)) ddashState.mainBatteryAvgTemp = (float)val;
        if (networkINTF->readParsedSignal("BPS_Regen_OK", val)) ddashState.bpsRegenOK = (val != 0);
        if (networkINTF->readParsedSignal("BPS_Charge_OK", val)) ddashState.bpsChargeOK = (val != 0);
        // Precharge voltages live in two separate McQueen CarCAN messages:
        //   BPS_Precharge_Voltages (ID 32)  → battery-side + array-side
        //   VCU_Precharge_Voltages (ID 33)  → battery-side + motor-side (see below)
        if (networkINTF->readParsedSignal("BPS_Precharge_Battery_Voltage", val)) ddashState.prechargeBatteryV = (float)val;
        if (networkINTF->readParsedSignal("BPS_Precharge_Array_Voltage", val)) ddashState.prechargeArrayV = (float)val;
        if (networkINTF->readParsedSignal("Main_Battery_Current_RawV", val)) ddashState.mainBatteryCurrentRawV = (float)val;
        if (networkINTF->readParsedSignal("BPS_Fault", val)) ddashState.bpsFaultCode = (uint8_t)val;

        // VCU status (CarCAN ID 24 in McQueen)
        if (networkINTF->readParsedSignal("VCU_FSM_State", val)) ddashState.vcuFsmState = (uint8_t)val;
        // No single VCU_Fault byte in McQueen — pack the five *_FAULT_DETECTED
        // bits into a bitfield (bit0=BPS, bit1=Controls, bit2=Mtr, bit3=Pedals, bit4=Steering).
        {
            uint8_t packed = 0;
            if (networkINTF->readParsedSignal("VCU_BPS_FAULT_DETECTED",      val) && val != 0) packed |= (1u << 0);
            if (networkINTF->readParsedSignal("VCU_CONTROLS_FAULT_DETECTED", val) && val != 0) packed |= (1u << 1);
            if (networkINTF->readParsedSignal("VCU_MTR_FAULT_DETECTED",      val) && val != 0) packed |= (1u << 2);
            if (networkINTF->readParsedSignal("VCU_PEDALS_FAULT_DETECTED",   val) && val != 0) packed |= (1u << 3);
            if (networkINTF->readParsedSignal("VCU_STEERING_FAULT_DETECTED", val) && val != 0) packed |= (1u << 4);
            ddashState.vcuFaultCode = packed;
        }
        // Motor_Ready signal replaces Motor_Ready_To_Drive in McQueen VCU_Status.
        if (networkINTF->readParsedSignal("Motor_Ready", val)) ddashState.motorReadyToDrive = (val != 0);
        // Derive "OK" flags from the McQueen watchdog bits — watchdog=1 means stale/fault.
        if (networkINTF->readParsedSignal("VCU_Pedals_Watchdog", val))       ddashState.vcuPedalsOK = (val == 0);
        if (networkINTF->readParsedSignal("VCU_Driver_Input_Watchdog", val)) ddashState.vcuDriverInputOK = (val == 0);
        if (networkINTF->readParsedSignal("VCU_Regen_Active", val)) ddashState.vcuRegenActive = (val != 0);
        if (networkINTF->readParsedSignal("VCU_Regen_OK", val)) ddashState.vcuRegenOK = (val != 0);
        // Motor-side precharge voltage lives in VCU_Precharge_Voltages (ID 33) in McQueen.
        if (networkINTF->readParsedSignal("VCU_Precharge_Motor_Voltage", val)) ddashState.prechargeMotorV = (float)val;

        // MPPT solar - 3 channels (A=512/513, B=544/545, C=576/577 in McQueen).
        // McQueen renames the power-measurement signals. MPPT_Mode/Fault/Enabled
        // plus temperatures are shared names across all three channels.
        if (networkINTF->readParsedSignal("MPPT_Input_Voltage", val))  ddashState.mppt[0].vin  = (float)val;
        if (networkINTF->readParsedSignal("MPPT_Input_Current", val))  ddashState.mppt[0].iin  = (float)val;
        if (networkINTF->readParsedSignal("MPPT_Output_Voltage", val)) ddashState.mppt[0].vout = (float)val;
        if (networkINTF->readParsedSignal("MPPT_Output_Current", val)) ddashState.mppt[0].iout = (float)val;
        if (networkINTF->readParsedSignal("MPPT_HeatsinkTemperature", val)) ddashState.mppt[0].heatsinkTemp = (float)val;
        if (networkINTF->readParsedSignal("MPPT_AmbientTemperature", val)) ddashState.mppt[0].ambientTemp = (float)val;
        if (networkINTF->readParsedSignal("MPPT_Fault", val)) ddashState.mppt[0].fault = (uint8_t)val;
        if (networkINTF->readParsedSignal("MPPT_Mode", val)) ddashState.mppt[0].mode = (uint8_t)val;
        if (networkINTF->readParsedSignal("MPPT_Enabled", val)) ddashState.mppt[0].enabled = (val != 0);
        // Note: MPPT B and C share signal names, so DBC parser will overwrite with latest
        // All 3 channels end up in mppt[0] via the shared signal names

        // Cooling system
        if (networkINTF->readParsedSignal("Coolant_Temperature_1", val)) ddashState.coolantTemp1 = (float)val;
        if (networkINTF->readParsedSignal("Coolant_Temperature_2", val)) ddashState.coolantTemp2 = (float)val;
        if (networkINTF->readParsedSignal("FlowRate_1", val)) ddashState.flowRate1 = (float)val;
        if (networkINTF->readParsedSignal("FlowRate_2", val)) ddashState.flowRate2 = (float)val;
        if (networkINTF->readParsedSignal("Pump_DutyCycle", val)) ddashState.pumpDuty = (uint8_t)val;
        if (networkINTF->readParsedSignal("Pump_Fault", val)) ddashState.pumpFault = (val != 0);

        // Steering wheel
        if (networkINTF->readParsedSignal("LWS_Angle", val)) ddashState.steeringAngle = (float)val;
        // McQueen SteeringCAN exposes LWS_Fault (1 = fault). Invert to derive OK.
        if (networkINTF->readParsedSignal("LWS_Fault", val)) ddashState.steeringSensorOK = (val == 0);

        // Supp battery charger. McQueen names the charger status / DCDC rails
        // after the Vicor module in Supp_Charging_Status (ID 769).
        if (networkINTF->readParsedSignal("Supplemental_Charging_Status", val)) ddashState.suppChargerStatus = (uint8_t)val;
        if (networkINTF->readParsedSignal("Supplemental_Vicor_Voltage", val)) ddashState.suppDcdcVoltage = (float)val;
        if (networkINTF->readParsedSignal("Supplemental_Vicor_Current", val)) ddashState.suppDcdcCurrent = (float)val;

        // Pedal sensor details — McQueen renamed Accel_Pos_* / Brake_Pos_*
        // into the AccelPedal_*_Pos / BrakePedal_*_Pos form in Pedal_Status (ID 80).
        if (networkINTF->readParsedSignal("AccelPedal_Main_Pos", val)) ddashState.accelPosMain = (uint8_t)val;
        if (networkINTF->readParsedSignal("AccelPedal_Redundant_Pos", val)) ddashState.accelPosRedundant = (uint8_t)val;
        if (networkINTF->readParsedSignal("BrakePedal_Main_Pos", val)) ddashState.brakePosMain = (uint8_t)val;
        if (networkINTF->readParsedSignal("BrakePedal_Redundant_Pos", val)) ddashState.brakePosRedundant = (uint8_t)val;
        if (networkINTF->readParsedSignal("AccelPedal_Main_Fault", val)) ddashState.accelMainFault = (val != 0);
        if (networkINTF->readParsedSignal("AccelPedal_Redundant_Fault", val)) ddashState.accelRedundantFault = (val != 0);
        if (networkINTF->readParsedSignal("BrakePedal_Main_Fault", val)) ddashState.brakeMainFault = (val != 0);
        if (networkINTF->readParsedSignal("BrakePedal_Redundant_Fault", val)) ddashState.brakeRedundantFault = (val != 0);
        if (networkINTF->readParsedSignal("Brake_Pressure_1_Fault", val)) ddashState.brakePressure1Fault = (val != 0);
        if (networkINTF->readParsedSignal("Brake_Pressure_2_Fault", val)) ddashState.brakePressure2Fault = (val != 0);

        // Pedal voltages (ID 81)
        if (networkINTF->readParsedSignal("Accel_Pos_Voltage_Main", val)) ddashState.accelVoltMain = (float)val;
        if (networkINTF->readParsedSignal("Accel_Pos_Voltage_Redundant", val)) ddashState.accelVoltRedundant = (float)val;
        if (networkINTF->readParsedSignal("Brake_Pos_Voltage_Main", val)) ddashState.brakeVoltMain = (float)val;
        if (networkINTF->readParsedSignal("Brake_Pos_Voltage_Redundant", val)) ddashState.brakeVoltRedundant = (float)val;

        // Brake pressure sensors (ID 1616)
        if (networkINTF->readParsedSignal("Brake_Pressure_1", val)) ddashState.brakePressure1 = (float)val;
        if (networkINTF->readParsedSignal("Brake_Pressure_2", val)) ddashState.brakePressure2 = (float)val;
        if (networkINTF->readParsedSignal("Brake_Pressure_1_Voltage", val)) ddashState.brakePressure1V = (float)val;
        if (networkINTF->readParsedSignal("Brake_Pressure_2_Voltage", val)) ddashState.brakePressure2V = (float)val;

        // Driver input buttons (ID 96)
        if (networkINTF->readParsedSignal("Horn_Pressed", val)) ddashState.hornPressed = (val != 0);
        if (networkINTF->readParsedSignal("Hazard_Pressed", val)) ddashState.hazardPressed = (val != 0);
        if (networkINTF->readParsedSignal("PushToTalk_Pressed", val)) ddashState.pttPressed = (val != 0);
        if (networkINTF->readParsedSignal("Cruise_Set", val)) ddashState.cruiseSet = (val != 0);
        if (networkINTF->readParsedSignal("Regen_Activate", val)) ddashState.regenActivate = (val != 0);

        // LV carrier (ID 1536)
        if (networkINTF->readParsedSignal("LTC4421_HVDCDC_Selected", val)) ddashState.lvHvDcdcSelected = (val != 0);
        if (networkINTF->readParsedSignal("LTC4421_HVDCDC_Fault", val)) ddashState.lvHvDcdcFault = (val != 0);
        if (networkINTF->readParsedSignal("LTC4421_HVDCDC_Valid", val)) ddashState.lvHvDcdcValid = (val != 0);
        if (networkINTF->readParsedSignal("LTC4421_SuppBatt_Selected", val)) ddashState.lvSuppBattSelected = (val != 0);
        if (networkINTF->readParsedSignal("LTC4421_SuppBatt_Fault", val)) ddashState.lvSuppBattFault = (val != 0);
        if (networkINTF->readParsedSignal("LTC4421_SuppBatt_Valid", val)) ddashState.lvSuppBattValid = (val != 0);
        if (networkINTF->readParsedSignal("LV_EN_SupplementalBattery", val)) ddashState.lvEnSuppBattery = (val != 0);
        if (networkINTF->readParsedSignal("LV_EN_PowerSupply", val)) ddashState.lvEnPowerSupply = (val != 0);

        // Camera status (ID 1792)
        if (networkINTF->readParsedSignal("Camera_Status_Backup", val)) ddashState.cameraBackup = (val != 0);
        if (networkINTF->readParsedSignal("Camera_Status_Left", val)) ddashState.cameraLeft = (val != 0);
        if (networkINTF->readParsedSignal("Camera_Status_Right", val)) ddashState.cameraRight = (val != 0);
        if (networkINTF->readParsedSignal("Display_FrameRate", val)) ddashState.displayFps = (uint8_t)val;

        // Lighting / controls faults (ID 25)
        if (networkINTF->readParsedSignal("Controls_Lighting_Fault", val)) ddashState.lightingFaults = (uint8_t)val;
        if (networkINTF->readParsedSignal("Controls_Leader_Fault", val)) ddashState.controlsLeaderFault = (uint8_t)val;
    }
    ddashState.leftCameraTexture = leftCameraTexture;
    ddashState.rightCameraTexture = rightCameraTexture;
    ui::RenderUI(ddashState);

    if (!dashboardOnly) {
        slcanWindow();
        customBackground();
        ImPlot::ShowDemoWindow();
        ImPlot3D::ShowDemoWindow();
        fpsWindow();
        customShaderWindow();
        showVideoDisplay();
        networkSamplePlot();
    }

    ImGui::Render();
}

void UI::fpsWindow(){
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 padding(12.0f, 12.0f);
    ImVec2 windowPos = ImVec2(io.DisplaySize.x - padding.x, padding.y);
    ImVec2 windowPivot = ImVec2(1.0f, 0.0f);
    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, windowPivot);
    ImGui::SetNextWindowBgAlpha(0.0f);

    float ft = io.DeltaTime * 1000.0f;
    for (size_t i = 1; i < renderSettings.frameTimes.size(); ++i) {
        renderSettings.frameTimes[i - 1] = renderSettings.frameTimes[i];
    }
    renderSettings.frameTimes[renderSettings.frameTimes.size() - 1] = ft;
    renderSettings.frameTimeMin = 9999.0f;
    renderSettings.frameTimeMax = 0.0f;
    for (float v : renderSettings.frameTimes) {
        renderSettings.frameTimeMin = std::min(renderSettings.frameTimeMin, v);
        renderSettings.frameTimeMax = std::max(renderSettings.frameTimeMax, v);
    }
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    // Stats window
    if (ImGui::Begin("Photon Stats", nullptr, windowFlags)) {
        ImGuiIO &io = ImGui::GetIO();
        float fps = io.Framerate;
        float ft_ms = (io.DeltaTime > 0.0f) ? (io.DeltaTime * 1000.0f) : 0.0f;
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Text("Frame time: %.3f ms", ft_ms);
        ImGui::Separator();
        ImGui::Text("Renderer: %s", deviceName[0] ? deviceName : "Unknown");
        ImGui::Text("VendorID: 0x%04X  DeviceID: 0x%04X", vendorID, deviceID);
        const char* typeStr = "Other";
        switch (deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: typeStr = "Integrated GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: typeStr = "Discrete GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: typeStr = "Virtual GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU: typeStr = "CPU"; break;
            default: break;
        }
        ImGui::Text("Device Type: %s", typeStr);
        ImGui::Text("Driver: %u  API: %u.%u.%u",
            driverVersion,
            VK_API_VERSION_MAJOR(apiVersion),
            VK_API_VERSION_MINOR(apiVersion),
            VK_API_VERSION_PATCH(apiVersion));
        ImGui::Separator();
        ImGui::Text("Frametime (last %zu):", renderSettings.frameTimes.size());
        ImGui::PlotLines("##ft", renderSettings.frameTimes.data(), (int)renderSettings.frameTimes.size(), 0,
                         nullptr, renderSettings.frameTimeMin, renderSettings.frameTimeMax,
                         ImVec2(240, 80));
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
}

void UI::customShaderWindow(){
    if (!customShader.texture) { return; }

    ImGui::SetNextWindowSize(ImVec2(customShader.x, customShader.y), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Custom Shader")) {
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        if (contentSize.x <= 1.0f || contentSize.y <= 1.0f) {
            contentSize = ImVec2(customShader.x, customShader.y);
        }

        const float epsilon = 0.5f;
        if (contentSize.x > 1.0f && contentSize.y > 1.0f) {
            if (std::fabs(contentSize.x - customShader.x) > epsilon ||
                std::fabs(contentSize.y - customShader.y) > epsilon) {
                customShader.x = contentSize.x;
                customShader.y = contentSize.y;
                customShader.dirty = true;
            }
        }

        ImVec2 drawSize(customShader.x, customShader.y);
        drawSize.x = std::max(drawSize.x, 1.0f);
        drawSize.y = std::max(drawSize.y, 1.0f);
        ImGui::Image(customShader.texture, drawSize);
    }
    ImGui::End();
}

void UI::showVideoDisplay(){
    if (!videoTexture) { return; }
    if (ImGui::Begin("Custom Image", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImVec2 size = videoTextureSize;
        if (size.x <= 0.0f || size.y <= 0.0f) { size = ImVec2(512.0f, 512.0f); }
        ImVec2 available = ImGui::GetContentRegionAvail();
        ImVec2 drawSize = size;
        if (available.x > 0.0f && available.y > 0.0f) {
            float scaleX = available.x / size.x;
            float scaleY = available.y / size.y;
            float scale = scaleX < scaleY ? scaleX : scaleY;
            if (scale < 1.0f) {
                drawSize.x = size.x * scale;
                drawSize.y = size.y * scale;
            }
        }
        ImGui::Image(videoTexture, drawSize);
    }
    ImGui::End();
}

void UI::networkSamplePlot(){
    ImGui::SetNextWindowSize(ImVec2(460.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Network Samples")) {
        ImGui::End();
        return;
    }

    if (!networkINTF) {
        ImGui::TextUnformatted("Network interface unavailable.");
        ImGui::End();
        return;
    }

    const uint16_t sampleCanId = 0x07FF;

    struct ScrollingBuffer {
        int MaxSize;
        int Offset;
        ImVector<ImVec2> Data;
        ScrollingBuffer(int maxSize = 2400) : MaxSize(maxSize), Offset(0) {
            Data.reserve(MaxSize);
        }
        void AddPoint(float x, float y) {
            if (Data.size() < static_cast<size_t>(MaxSize)) {
                Data.push_back(ImVec2(x, y));
            } else {
                Data[Offset] = ImVec2(x, y);
                Offset = (Offset + 1) % MaxSize;
            }
        }
        void Clear() {
            Data.shrink(0);
            Offset = 0;
        }
    };

    static ScrollingBuffer sampleHistory;
    static uint64_t lastSampleValue = 0;
    static bool haveSample = false;
    static float historySeconds = 10.0f;
    static float accumulatedTime = 0.0f;

    ImGui::Text("CAN 0x%03X", sampleCanId);
    ImGui::SliderFloat("History", &historySeconds, 1.0f, 60.0f, "%.1f s");

    ImGuiIO &io = ImGui::GetIO();
    accumulatedTime += io.DeltaTime;

    uint64_t rawValue = 0;
    if (networkINTF->readSample(sampleCanId, rawValue)) {
        lastSampleValue = rawValue;
        haveSample = true;
    }

    if (haveSample) {
        sampleHistory.AddPoint(accumulatedTime, static_cast<float>(lastSampleValue));
    }

    if (!haveSample || sampleHistory.Data.empty()) {
        ImGui::Text("Waiting for samples...");
        ImGui::End();
        return;
    }

    ImGui::Text("Last value: 0x%016llX (%llu)",
                static_cast<unsigned long long>(lastSampleValue),
                static_cast<unsigned long long>(lastSampleValue));

    float yMin = sampleHistory.Data[0].y;
    float yMax = sampleHistory.Data[0].y;
    for (const ImVec2& point : sampleHistory.Data) {
        yMin = std::min(yMin, point.y);
        yMax = std::max(yMax, point.y);
    }
    if (yMin == yMax) {
        yMax = yMin + 1.0f;
        yMin = yMin - 1.0f;
    } else {
        const float padding = (yMax - yMin) * 0.05f;
        yMin -= padding;
        yMax += padding;
    }

    const float plotStartTime = std::max(accumulatedTime - historySeconds, 0.0f);

    if (ImPlot::BeginPlot("##network_samples", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Time (s)", "Value", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_X1, plotStartTime, accumulatedTime, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, yMin, yMax, ImGuiCond_Always);
        ImPlot::SetNextLineStyle(ImGui::GetStyle().Colors[ImGuiCol_PlotLines], 2.0f);
        ImPlot::PlotLine("Sample", &sampleHistory.Data[0].x, &sampleHistory.Data[0].y,
                         static_cast<int>(sampleHistory.Data.size()), 0, sampleHistory.Offset, sizeof(ImVec2));
        ImPlot::EndPlot();
    }

    ImGui::End();
}

void UI::slcanWindow() {
    ImGui::SetNextWindowSize(ImVec2(460.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("SLCAN Monitor")) {
        ImGui::End();
        return;
    }

    if (!networkINTF) {
        ImGui::Text("Network not connected.");
        ImGui::End();
        return;
    }

    auto& net = *networkINTF;

    if (ImGui::BeginTable("slcan_table", 3,
                          ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_RowBg)) {

        ImGui::TableSetupColumn("CAN ID");
        ImGui::TableSetupColumn("Raw");
        ImGui::TableSetupColumn("Interpretation");
        ImGui::TableHeadersRow();

        std::lock_guard<std::mutex> lock(net.decodedHistoryMutex);

        for (const auto& entry : net.decodedHistory) {

            ImGui::TableNextRow();

            // CAN ID
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%u (0x%X)", entry.canId, entry.canId);

            // Raw Value
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("0x%016llX",
                (unsigned long long)entry.rawValue);

            // Interpretation (multiline)
            ImGui::TableSetColumnIndex(2);
            for (const std::string& line : entry.lines) {
                ImGui::TextUnformatted(line.c_str());
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}




void UI::customBackground(){
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;
    if (displaySize.x > 0.0f && displaySize.y > 0.0f) {
        const float epsilon = 0.5f;
        if (std::fabs(background.x - displaySize.x) > epsilon ||
            std::fabs(background.y - displaySize.y) > epsilon) {
            background.x = displaySize.x;
            background.y = displaySize.y;
            background.dirty = true;
        }
    }

    if (!background.texture) { return; }

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    if (!viewport) { return; }

    ImDrawList *drawList = ImGui::GetBackgroundDrawList(viewport);
    ImVec2 min = viewport->Pos;
    ImVec2 max = ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
    drawList->AddImage(this->background.texture, min, max);
}

void UI::setStyle(){
    static bool ddashInitialized = false;
    if (!ddashInitialized) {
        // Must happen before the first ImGui::NewFrame().
        // Calling this inside the render loop triggers the "locked ImFontAtlas" assert.
        ui::InitUI();
        ddashInitialized = true;
    }
}
