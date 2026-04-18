#pragma once

/**
 * Vehicle Dashboard ImGui UI
 * 
 * Main integration header - include this in your render loop
 */

#include "state.h"
#include "theme.h"
#include "widgets.h"
#include "dashboard.h"

#include <cmath>
#include <cstdlib>

namespace ui {

/**
 * Initialize the UI system
 * Call once after creating ImGui context
 * 
 * @param fontPath Optional path to custom UI font
 * @param monoFontPath Optional path to monospace font
 * @param iconFontPath Optional path to icon font (Font Awesome, etc.)
 */
inline void InitUI(const char* fontPath = nullptr, 
                   const char* monoFontPath = nullptr,
                   const char* iconFontPath = nullptr) {
    ApplyTheme();
    LoadFonts(fontPath, monoFontPath, iconFontPath);
}

/**
 * Render the complete UI
 * Call this from your main render loop
 * 
 * @param state Application state (read/write access for interaction)
 * 
 * Example usage:
 * @code
 *   // In your render loop:
 *   ImGui_ImplXXX_NewFrame();
 *   ImGui::NewFrame();
 *   
 *   ui::RenderUI(appState);
 *   
 *   ImGui::Render();
 *   ImGui_ImplXXX_RenderDrawData(ImGui::GetDrawData());
 * @endcode
 */
inline void RenderUI(AppState& state) {
    RenderDashboard(state);
}

/**
 * Call this at regular intervals to update state
 * 
 * @param state Application state to update
 * @param deltaTime Time since last update in seconds
 */
inline void UpdateSimulation(AppState& state, float deltaTime) {
    static float t = 0.0f;
    t += deltaTime;

    state.heartbeat = static_cast<uint8_t>((state.heartbeat + 1) % 256);

    // Speed: smooth sine between 0 and 80 km/h, period ~16s.
    float s = 0.5f - 0.5f * std::cos(t * 0.4f);
    state.speed = static_cast<int>(s * 80.0f);

    // Gear: F most of the time, brief N/R phases.
    int gearPhase = static_cast<int>(t) / 15;
    switch (gearPhase % 3) {
        case 0: state.gear = Gear::Forward; break;
        case 1: state.gear = Gear::Neutral; break;
        case 2: state.gear = Gear::Reverse; break;
    }

    // Turn signal cycle: none -> left -> none -> right.
    int turnPhase = static_cast<int>(t * 0.5f) % 4;
    switch (turnPhase) {
        case 0: state.turnSignal = TurnSignal::None;  break;
        case 1: state.turnSignal = TurnSignal::Left;  break;
        case 2: state.turnSignal = TurnSignal::None;  break;
        case 3: state.turnSignal = TurnSignal::Right; break;
    }

    // Main battery: triangle wave between 20% and 100% -- drain for 2 min,
    // charge for 2 min, repeat. Smooth, no snap.
    float cycle = std::fmod(t, 240.0f);
    float soc = (cycle < 120.0f)
                    ? 100.0f - cycle * (80.0f / 120.0f)
                    : 20.0f + (cycle - 120.0f) * (80.0f / 120.0f);
    state.mainBattery.soc = soc;
    state.mainBattery.voltage = 100.0f + (soc / 100.0f) * 30.0f;          // 100-130V-ish
    state.mainBattery.current = -40.0f + 30.0f * std::sin(t * 0.8f);       // regen / draw
    state.mainBatteryAvgTemp = 28.0f + 5.0f * std::sin(t * 0.3f);

    // Supp (12V aux) battery: steady around 12.6V with slow ripple.
    state.suppBattery.soc     = 85.0f + 5.0f * std::sin(t * 0.2f);
    state.suppBattery.voltage = 12.6f + 0.2f * std::sin(t * 0.7f);
    state.suppBattery.current = 1.5f  + 0.5f * std::sin(t * 0.9f);

    // Motor controller: heatsink warms with speed, bus tracks pack.
    state.motorController.heatsinkTemp = 35.0f + (state.speed * 0.5f);
    state.motorController.voltage      = state.mainBattery.voltage;
    state.motorController.current      = std::abs(state.mainBattery.current);
    state.motorController.phaseCurrentB = 10.0f * std::sin(t * 3.0f);
    state.motorController.phaseCurrentC = 10.0f * std::sin(t * 3.0f + 2.09f);
    state.motorController.backEmfQ      = 5.0f * std::sin(t * 1.5f);
    state.motorController.backEmfD      = 5.0f * std::cos(t * 1.5f);

    // Cruise: toggle on/off every 30s, set to a round number.
    state.cruise.enabled  = (static_cast<int>(t) / 30) & 1;
    state.cruise.setSpeed = 55;

    // Regen + brake: brief pulses.
    float brakeCycle = std::fmod(t, 10.0f);
    state.brakeEngaged  = (brakeCycle > 8.0f);
    state.regenEnabled  = (brakeCycle > 2.0f && brakeCycle < 5.0f);

    // Contactors: power-up sequence staggered across 6s, then steady.
    float ct = std::fmod(t, 30.0f);
    state.contactorStates.hvPositive     = (ct > 1.0f);
    state.contactorStates.hvNegative     = (ct > 1.2f);
    state.contactorStates.arrayPrecharge = (ct > 2.0f && ct < 4.0f);
    state.contactorStates.arrayContactor = (ct > 3.5f);
    state.contactorStates.motorPrecharge = (ct > 4.5f && ct < 6.5f);
    state.contactorStates.motorContactor = (ct > 6.0f);

    // Ignition switches: LV first, then Array, then Motor.
    state.ignitionStates.lvEnabled    = (ct > 0.2f);
    state.ignitionStates.arrayEnabled = (ct > 3.0f);
    state.ignitionStates.motorEnabled = (ct > 5.0f);

    // Pedal / brake pressure sim
    state.pedalPercent    = 30.0f + 20.0f * std::sin(t * 1.1f);
    state.brakePressure   = state.brakeEngaged ? (500.0f + 100.0f * std::sin(t * 4.0f)) : 0.0f;
    state.brakePressure1  = state.brakePressure;
    state.brakePressure2  = state.brakePressure * 0.98f;
    state.brakePressure1V = 1.0f + state.brakePressure / 1000.0f;
    state.brakePressure2V = 1.0f + state.brakePressure / 1000.0f;

    // MPPT channels
    for (int i = 0; i < 3; ++i) {
        float phase = t + i * 2.0f;
        state.mppt[i].enabled      = true;
        state.mppt[i].vin          = 50.0f + 5.0f * std::sin(phase * 0.4f);
        state.mppt[i].iin          = 3.0f  + 1.0f * std::sin(phase * 0.6f);
        state.mppt[i].vout         = state.mainBattery.voltage;
        state.mppt[i].iout         = state.mppt[i].iin * state.mppt[i].vin / state.mainBattery.voltage;
        state.mppt[i].heatsinkTemp = 40.0f + 5.0f * std::sin(phase * 0.3f);
        state.mppt[i].ambientTemp  = 25.0f + 3.0f * std::sin(phase * 0.2f);
    }

    // Cooling
    state.coolantTemp1 = 30.0f + state.speed * 0.2f;
    state.coolantTemp2 = state.coolantTemp1 + 1.5f;
    state.flowRate1    = 8.0f;
    state.flowRate2    = 8.2f;
    state.pumpDuty     = static_cast<uint8_t>(40 + state.speed * 0.7f);

    // Steering
    state.steeringAngle      = 20.0f * std::sin(t * 0.6f);
    state.steeringSensorOK   = true;

    // BPS / VCU OK states
    state.bpsRegenOK         = true;
    state.bpsChargeOK        = true;
    state.motorReadyToDrive  = state.contactorStates.motorContactor;
    state.vcuPedalsOK        = true;
    state.vcuDriverInputOK   = true;
    state.vcuRegenOK         = true;
    state.vcuRegenActive     = state.regenEnabled;

    // Clear any stale fault flags so the sim looks healthy.
    state.canFault            = false;
    state.canFaultRecoverable = false;
    state.bpsFaultCode        = 0;
    state.vcuFaultCode        = 0;
    state.lightingFaults      = 0;
    state.controlsLeaderFault = 0;
}

} // namespace ui
