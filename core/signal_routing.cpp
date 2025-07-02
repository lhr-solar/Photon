#include "signal_routing.hpp"
#include <implot.h>
#include <implot3d.h>
#include <string>
#include <cmath>
#include <algorithm>

DbcPlotRegistry g_plot_registry;
PlotDrawerRegistry g_plot_drawers;

PlotDrawerRegistry::PlotDrawerRegistry() {
    default_drawer = [](const std::string& name,
                        const std::vector<PlotSignal>& signals,
                        const SignalDataMap& times,
                        const SignalDataMap& values){
        if(ImPlot::BeginPlot(name.c_str())){
            double y_min = 0.0, y_max = 0.0;
            bool first = true;
            for (const auto& sig : signals) {
                auto ity = values.find(sig.first);
                if (ity == values.end() || ity->second.empty())
                    continue;
                auto minmax = std::minmax_element(ity->second.begin(), ity->second.end());
                if (first) {
                    y_min = *minmax.first;
                    y_max = *minmax.second;
                    first = false;
                }
                else {
                    y_min = std::min(y_min, *minmax.first);
                    y_max = std::max(y_max, *minmax.second);
                }
            }
            ImPlot::SetupAxes("Time", "Value", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
            if (!first)
                ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, y_max, ImGuiCond_Always);
            for(const auto &sig : signals){
                auto itx = times.find(sig.first);
                auto ity = values.find(sig.first);
                if(itx == times.end() || ity == values.end())
                    continue;
                ImPlot::PlotLine(sig.second.c_str(), itx->second.data(), ity->second.data(), ity->second.size());
            }
            ImPlot::EndPlot();
        }
    };
}

void PlotDrawerRegistry::register_drawer(const std::string& name, PlotDrawFn fn){
    drawers[name] = std::move(fn);
}

PlotDrawFn PlotDrawerRegistry::get_drawer(const std::string& name) const {
    auto it = drawers.find(name);
    if(it != drawers.end())
        return it->second;
    return default_drawer;
}

static PlotDrawFn make_line_drawer(const char* y_label) {
    return [y_label](const std::string& name,
                     const std::vector<PlotSignal>& signals,
                     const SignalDataMap& times,
                     const SignalDataMap& values){
        if(ImPlot::BeginPlot(name.c_str())){
            double y_min = 0.0, y_max = 0.0;
            bool first_pt = true;
            for (const auto& sig : signals) {
                auto ity = values.find(sig.first);
                if (ity == values.end() || ity->second.empty())
                    continue;
                auto mm = std::minmax_element(ity->second.begin(), ity->second.end());
                if (first_pt) {
                    y_min = *mm.first;
                    y_max = *mm.second;
                    first_pt = false;
                }
                else {
                    y_min = std::min(y_min, *mm.first);
                    y_max = std::max(y_max, *mm.second);
                }
            }
            ImPlot::SetupAxes("Time", y_label, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
            if (!first_pt)
                ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, y_max, ImGuiCond_Always);
            for(const auto &sig : signals){
                auto itx = times.find(sig.first);
                auto ity = values.find(sig.first);
                if(itx == times.end() || ity == values.end())
                    continue;
                ImPlot::PlotLine(sig.second.c_str(), itx->second.data(), ity->second.data(), ity->second.size());
            }
            ImPlot::EndPlot();
        }
    };
}

static PlotDrawFn make_digital_drawer(const char* y_label) {
    return [y_label](const std::string& name,
                     const std::vector<PlotSignal>& signals,
                     const SignalDataMap& times,
                     const SignalDataMap& values){
        if(ImPlot::BeginPlot(name.c_str())){
            ImPlot::SetupAxes("Time", y_label, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            for(const auto &sig : signals){
                auto itx = times.find(sig.first);
                auto ity = values.find(sig.first);
                if(itx == times.end() || ity == values.end())
                    continue;
                ImPlot::PlotDigital(sig.second.c_str(), itx->second.data(), ity->second.data(), ity->second.size());
            }
            ImPlot::EndPlot();
        }
    };
}

static PlotDrawFn make_histogram_drawer(const char* x_label) {
    return [x_label](const std::string& name,
                     const std::vector<PlotSignal>& signals,
                     const SignalDataMap& times,
                     const SignalDataMap& values){
        if(ImPlot::BeginPlot(name.c_str())){
            ImPlot::SetupAxes(x_label, "Count", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            for(const auto &sig : signals){
                auto ity = values.find(sig.first);
                if(ity == values.end())
                    continue;
                ImPlot::PlotHistogram(sig.second.c_str(), ity->second.data(), ity->second.size(), 50);
            }
            ImPlot::EndPlot();
        }
    };
}

static PlotDrawFn make_histogram2d_drawer(const char* label) {
    return [label](const std::string& name,
                   const std::vector<PlotSignal>& signals,
                   const SignalDataMap& times,
                   const SignalDataMap& values){
        if(signals.size() < 2)
            return;
        auto itx = values.find(signals[0].first);
        auto ity = values.find(signals[1].first);
        if(itx == values.end() || ity == values.end())
            return;
        if(itx->second.empty() || ity->second.empty())
            return;
        if(ImPlot::BeginPlot(name.c_str())){
            ImPlot::SetupAxes(signals[0].second.c_str(), signals[1].second.c_str(), ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotHistogram2D(label,
                                   itx->second.data(), ity->second.data(), itx->second.size(), 50);
            ImPlot::EndPlot();
        }
    };
}

static PlotDrawFn make_heatmap_drawer(const char* label) {
    return [label](const std::string& name,
                   const std::vector<PlotSignal>& signals,
                   const SignalDataMap& times,
                   const SignalDataMap& values){
        if(signals.empty())
            return;
        auto it = values.find(signals[0].first);
        if(it == values.end() || it->second.empty())
            return;
        const auto &data = it->second;
        int n = static_cast<int>(sqrt(data.size()));
        if(n*n != (int)data.size())
            return;
        if(ImPlot::BeginPlot(name.c_str())){
            ImPlot::SetupAxes("X", "Y", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotHeatmap(label, data.data(), n, n, 0, 0, nullptr);
            ImPlot::EndPlot();
        }
    };
}

static PlotDrawFn make_bar_drawer(const char* y_label) {
    return [y_label](const std::string& name,
                     const std::vector<PlotSignal>& signals,
                     const SignalDataMap& times,
                     const SignalDataMap& values){
        if(ImPlot::BeginPlot(name.c_str())){
            ImPlot::SetupAxes("Index", y_label, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            int idx = 0;
            for(const auto &sig : signals){
                auto it = values.find(sig.first);
                if(it == values.end())
                    continue;
                ImPlot::PlotBars(sig.second.c_str(), it->second.data(), it->second.size(), 0.67, idx++);
            }
            ImPlot::EndPlot();
        }
    };
}

/*
static PlotDrawFn make_bar_groups_drawer(const char* y_label) {
    return [y_label](const std::string& name,
                     const std::vector<PlotSignal>& signals,
                     const SignalDataMap& times,
                     const SignalDataMap& values){
        if(signals.empty())
            return;
        std::vector<const double*> data;
        int count = -1;
        for(const auto &sig : signals){
            auto it = values.find(sig.first);
            if(it == values.end())
                return;
            if(count == -1)
                count = it->second.size();
            count = std::min(count, (int)it->second.size());
            data.push_back(it->second.data());
        }
        if(count <= 0)
            return;
        if(ImPlot::BeginPlot(name.c_str())){
            ImPlot::SetupAxes("Index", y_label, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotBarGroups(nullptr, data.data(), signals.size(), count, 0.67, 0);
            ImPlot::EndPlot();
        }
    };
}
*/

/*
static PlotDrawFn make_bar_stack_drawer(const char* y_label) {
    return [y_label](const std::string& name,
                     const std::vector<PlotSignal>& signals,
                     const SignalDataMap& times,
                     const SignalDataMap& values){
        if(signals.empty())
            return;
        std::vector<const double*> data;
        int count = -1;
        for(const auto &sig : signals){
            auto it = values.find(sig.first);
            if(it == values.end())
                return;
            if(count == -1)
                count = it->second.size();
            count = std::min(count, (int)it->second.size());
            data.push_back(it->second.data());
        }
        if(count <= 0)
            return;
        if(ImPlot::BeginPlot(name.c_str())){
            ImPlot::SetupAxes("Index", y_label, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotBarStacked(nullptr, data.data(), signals.size(), count, 0.67);
            ImPlot::EndPlot();
        }
    };
}
*/

static PlotDrawFn make_surface_drawer(const char* z_label) {
    return [z_label](const std::string& name,
                     const std::vector<PlotSignal>& signals,
                     const SignalDataMap& times,
                     const SignalDataMap& values){
        if(signals.size() < 3)
            return;
        auto itx = values.find(signals[0].first);
        auto ity = values.find(signals[1].first);
        auto itz = values.find(signals[2].first);
        if(itx == values.end() || ity == values.end() || itz == values.end())
            return;
        int count = std::min({itx->second.size(), ity->second.size(), itz->second.size()});
        if(count <= 0)
            return;
        int N = static_cast<int>(sqrt(count));
        if(N*N != count)
            return;
        if(ImPlot3D::BeginPlot(name.c_str())){
            ImPlot3D::SetupAxes(signals[0].second.c_str(), signals[1].second.c_str(), z_label);
            ImPlot3D::SetupAxesLimits(-1,1,-1,1,-1,1);
            ImPlot3D::PlotSurface(name.c_str(), itx->second.data(), ity->second.data(), itz->second.data(), N, N);
            ImPlot3D::EndPlot();
        }
    };
}

static PlotDrawFn make_line3d_drawer(const char* z_label) {
    return [z_label](const std::string& name,
                     const std::vector<PlotSignal>& signals,
                     const SignalDataMap& times,
                     const SignalDataMap& values){
        if(signals.size() < 3)
            return;
        auto itx = values.find(signals[0].first);
        auto ity = values.find(signals[1].first);
        auto itz = values.find(signals[2].first);
        if(itx == values.end() || ity == values.end() || itz == values.end())
            return;
        int count = std::min({itx->second.size(), ity->second.size(), itz->second.size()});
        if(count <= 0)
            return;
        if(ImPlot3D::BeginPlot(name.c_str())){
            ImPlot3D::SetupAxes(signals[0].second.c_str(), signals[1].second.c_str(), z_label);
            ImPlot3D::SetupAxesLimits(-1,1,-1,1,-1,1);
            ImPlot3D::PlotLine(name.c_str(), itx->second.data(), ity->second.data(), itz->second.data(), count);
            ImPlot3D::EndPlot();
        }
    };
}

void init_default_plot_registry() {
    /* Controls mappings */
    g_plot_registry.register_plot("Controls", 0x240, "Device_Serial_Number", "Moco. Info");
    g_plot_registry.register_plot("Controls", 0x240, "Prohelion_ID", "Moco. Info");

    g_plot_registry.register_plot("Controls", 0x241, "Rx_Error_Count", "Moco. Status");
    g_plot_registry.register_plot("Controls", 0x241, "Tx_Error_Count", "Moco. Status");
    g_plot_registry.register_plot("Controls", 0x241, "Active_Motor_Index", "Moco. Status");
    g_plot_registry.register_plot("Controls", 0x241, "Error_Flags", "Moco. Status");


    g_plot_registry.register_plot("Controls", 0x242, "Bus_Current", "Moco. Bus");
    g_plot_registry.register_plot("Controls", 0x242, "Bus_Voltage", "Moco. Bus");

    g_plot_registry.register_plot("Controls", 0x243, "Vehicle_Velocity", "Moco. Velocity");
    g_plot_registry.register_plot("Controls", 0x243, "Motor_Angular_Frequency", "Moco. Velocity");

    g_plot_registry.register_plot("Controls", 0x244, "Phase_C_Current", "Moco. Phase Currents");
    g_plot_registry.register_plot("Controls", 0x244, "Phase_B_Current", "Moco. Phase Currents");

    g_plot_registry.register_plot("Controls", 0x245, "Vd", "Moco. Voltage Vector");
    g_plot_registry.register_plot("Controls", 0x245, "Vq", "Moco. Voltage Vector");

    g_plot_registry.register_plot("Controls", 0x246, "Id", "Moco. Current Vector");
    g_plot_registry.register_plot("Controls", 0x246, "Iq", "Moco. Current Vector");

    g_plot_registry.register_plot("Controls", 0x247, "Real_Component", "Moco. Back EMF");
    g_plot_registry.register_plot("Controls", 0x247, "Peak_Phase_To_Neutral_Voltage", "Moco. Back EMF");

    g_plot_registry.register_plot("Controls", 0x248, "Actual_Voltage", "Low Voltage Rail");
    g_plot_registry.register_plot("Controls", 0x248, "Reserved", "Low Voltage Rail");

    g_plot_registry.register_plot("Controls", 0x249, "Actual_Voltage_3_3V", "Moco. DSP Voltage Rail");
    g_plot_registry.register_plot("Controls", 0x249, "Actual_Voltage_1_9V", "Moco. DSP Voltage Rail");

    g_plot_registry.register_plot("Controls", 0x580, "State_name", "State");

    g_plot_registry.register_plot("Controls", 0x582, "Motor_Precharge_enable", "Precharge");

    g_plot_registry.register_plot("Controls", 0x583, "Lakshay_Fault", "Faults");
    g_plot_registry.register_plot("Controls", 0x583, "OS_Fault", "Faults");
    g_plot_registry.register_plot("Controls", 0x583, "Internal_Controls_Fault", "Faults");
    g_plot_registry.register_plot("Controls", 0x583, "CarCANFault", "Faults");
    g_plot_registry.register_plot("Controls", 0x583, "Pedals_Fault", "Faults");
    g_plot_registry.register_plot("Controls", 0x583, "BPS_Fault", "Faults");
    g_plot_registry.register_plot("Controls", 0x583, "Motor_Controller_Fault", "Faults");
    g_plot_registry.register_plot("Controls", 0x583, "Any_Controls_Fault", "Faults");
    
    g_plot_registry.register_plot("Controls", 0x584, "Motor_Safe", "Moco. Safe");
    g_plot_registry.register_plot("Controls", 0x584, "Debug", "Moco. Safe");


    /* BPS mappings */
    g_plot_registry.register_plot("BPS", 0x2, "BPS_Trip", "Trip");
    g_plot_registry.register_plot("BPS", 0x101, "BPS_All_Clear", "All Clear");
    g_plot_registry.register_plot("BPS", 0x102, "Array_Contactor", "Contactors");
    g_plot_registry.register_plot("BPS", 0x102, "HV_Contactor", "Contactors");
    g_plot_registry.register_plot("BPS", 0x103, "Current", "Current");
    g_plot_registry.register_plot("BPS", 0x104, "Voltage_idx", "Voltage Array");
    g_plot_registry.register_plot("BPS", 0x104, "Voltage_Value", "Voltage Array");
    g_plot_registry.register_plot("BPS", 0x105, "Temperature_idx", "Temperature Array");
    g_plot_registry.register_plot("BPS", 0x105, "Temperature_Value", "Temperature Array");
    g_plot_registry.register_plot("BPS", 0x106, "SoC", "SOC");
    g_plot_registry.register_plot("BPS", 0x107, "WDog_Trig", "Watchdog");
    g_plot_registry.register_plot("BPS", 0x108, "BPS_CAN_Error", "CAN Error");
    g_plot_registry.register_plot("BPS", 0x109, "BPS_Command", "Command");
    g_plot_registry.register_plot("BPS", 0x10B, "Supplemental_Voltage", "Supplemental Voltage");
    g_plot_registry.register_plot("BPS", 0x10C, "Charge_Enabled", "Charge");
    g_plot_registry.register_plot("BPS", 0x10D, "Pack_Voltage", "Voltage Summary");
    g_plot_registry.register_plot("BPS", 0x10D, "Voltage_Range", "Voltage Summary");
    g_plot_registry.register_plot("BPS", 0x10D, "Voltage_Timestamp", "Voltage Summary");
    g_plot_registry.register_plot("BPS", 0x10E, "Average_Temp", "Temperature Summary");
    g_plot_registry.register_plot("BPS", 0x10E, "Temperature_Range", "Temperature Summary");
    g_plot_registry.register_plot("BPS", 0x10E, "Temperature_Timestamp", "Temperature Summary");
    g_plot_registry.register_plot("BPS", 0x10F, "BPS_Fault_State", "Fault State");

    /* Wavesculptor22 mappings */
    g_plot_registry.register_plot("Wavesculptor22", 0x80, "TritiumID", "Info");
    g_plot_registry.register_plot("Wavesculptor22", 0x80, "SerialNumber", "Info");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "LimitReserved", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "LimitIpmOrMotorTemp", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "LimitBusVoltageLower", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "LimitBusVoltageUpper", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "LimitBusCurrent", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "LimitVelocity", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "LimitMotorCurrent", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "LimitOutputVoltagePWM", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "ErrorReserved", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "ErrorMotorOverSpeed", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "ErrorDesaturationFault", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "Error15vRailUnderVoltage", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "ErrorConfigRead", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "ErrorWatchdogCausedLastReset", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "ErrorBadMotorPositionHallSeq", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "ErrorDcBusOverVoltage", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "ErrorSoftwareOverCurrent", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "ErrorHardwareOverCurrent", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "ActiveMotor", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "TxErrorCount", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x81, "RxErrorCount", "Status");
    g_plot_registry.register_plot("Wavesculptor22", 0x82, "BusVoltage", "Bus");
    g_plot_registry.register_plot("Wavesculptor22", 0x82, "BusCurrent", "Bus");
    g_plot_registry.register_plot("Wavesculptor22", 0x83, "MotorVelocity", "Velocity");
    g_plot_registry.register_plot("Wavesculptor22", 0x83, "VehicleVelocity", "Velocity");
    g_plot_registry.register_plot("Wavesculptor22", 0x84, "PhaseCurrentB", "Phase Currents");
    g_plot_registry.register_plot("Wavesculptor22", 0x84, "PhaseCurrentC", "Phase Currents");
    g_plot_registry.register_plot("Wavesculptor22", 0x85, "Vq", "Voltage Vector");
    g_plot_registry.register_plot("Wavesculptor22", 0x85, "Vd", "Voltage Vector");
    g_plot_registry.register_plot("Wavesculptor22", 0x86, "Iq", "Current Vector");
    g_plot_registry.register_plot("Wavesculptor22", 0x86, "Id", "Current Vector");
    g_plot_registry.register_plot("Wavesculptor22", 0x87, "BEMFq", "Back EMF");
    g_plot_registry.register_plot("Wavesculptor22", 0x87, "BEMFd", "Back EMF");
    g_plot_registry.register_plot("Wavesculptor22", 0x88, "Supply15V", "15V Rail");
    g_plot_registry.register_plot("Wavesculptor22", 0x88, "ReservedSupply15V", "15V Rail");
    g_plot_registry.register_plot("Wavesculptor22", 0x89, "Supply1V9", "DSP Voltage Rail");
    g_plot_registry.register_plot("Wavesculptor22", 0x89, "Supply3V3", "DSP Voltage Rail");
    g_plot_registry.register_plot("Wavesculptor22", 0x8A, "Reserved0A0", "Reserved0A");
    g_plot_registry.register_plot("Wavesculptor22", 0x8A, "Reserved0A1", "Reserved0A");
    g_plot_registry.register_plot("Wavesculptor22", 0x8B, "MotorTemp", "Temperatures");
    g_plot_registry.register_plot("Wavesculptor22", 0x8B, "HeatsinkTemp", "Temperatures");
    g_plot_registry.register_plot("Wavesculptor22", 0x8C, "DspBoardTemp", "DSP Board Temp");
    g_plot_registry.register_plot("Wavesculptor22", 0x8C, "ReservedDspBoardTemp", "DSP Board Temp");
    g_plot_registry.register_plot("Wavesculptor22", 0x8D, "Reserved0D0", "Reserved0D");
    g_plot_registry.register_plot("Wavesculptor22", 0x8D, "Reserved0D1", "Reserved0D");
    g_plot_registry.register_plot("Wavesculptor22", 0x8E, "Odometer", "Odometer");
    g_plot_registry.register_plot("Wavesculptor22", 0x8E, "DCBusAh", "Odometer");
    g_plot_registry.register_plot("Wavesculptor22", 0x97, "SlipSpeed", "Slip Speed");
    g_plot_registry.register_plot("Wavesculptor22", 0x97, "ReservedSlipSpeed", "Slip Speed");

    /* MPPT mappings */
    g_plot_registry.register_plot("MPPT", 0x201, "MPPT_Enabled", "MPPT32 Status");
    g_plot_registry.register_plot("MPPT", 0x201, "MPPT_HeatsinkTemperature", "MPPT32 Status");
    g_plot_registry.register_plot("MPPT", 0x201, "MPPT_AmbientTemperature", "MPPT32 Status");
    g_plot_registry.register_plot("MPPT", 0x201, "MPPT_Fault", "MPPT32 Status");
    g_plot_registry.register_plot("MPPT", 0x201, "MPPT_Mode", "MPPT32 Status");
    g_plot_registry.register_plot("MPPT", 0x200, "MPPT_Iout", "MPPT32 Power");
    g_plot_registry.register_plot("MPPT", 0x200, "MPPT_Vout", "MPPT32 Power");
    g_plot_registry.register_plot("MPPT", 0x200, "MPPT_Iin", "MPPT32 Power");
    g_plot_registry.register_plot("MPPT", 0x200, "MPPT_Vin", "MPPT32 Power");
    g_plot_registry.register_plot("MPPT", 0x211, "MPPT_Enabled", "MPPT33 Status");
    g_plot_registry.register_plot("MPPT", 0x211, "MPPT_HeatsinkTemperature", "MPPT33 Status");
    g_plot_registry.register_plot("MPPT", 0x211, "MPPT_AmbientTemperature", "MPPT33 Status");
    g_plot_registry.register_plot("MPPT", 0x211, "MPPT_Fault", "MPPT33 Status");
    g_plot_registry.register_plot("MPPT", 0x211, "MPPT_Mode", "MPPT33 Status");
    g_plot_registry.register_plot("MPPT", 0x210, "MPPT_Iout", "MPPT33 Power");
    g_plot_registry.register_plot("MPPT", 0x210, "MPPT_Vout", "MPPT33 Power");
    g_plot_registry.register_plot("MPPT", 0x210, "MPPT_Iin", "MPPT33 Power");
    g_plot_registry.register_plot("MPPT", 0x210, "MPPT_Vin", "MPPT33 Power");

    /* DAQ mappings */
    g_plot_registry.register_plot("DAQ", 0x701, "Bytes_Transmited", "RF Stats");
    g_plot_registry.register_plot("DAQ", 0x702, "TX_Fail_Count", "RF Stats");
    g_plot_registry.register_plot("DAQ", 0x704, "Good_Packet_Receive_Count", "RF Stats");
    g_plot_registry.register_plot("DAQ", 0x705, "MAC_ACK_Fail_Count", "RF Stats");
    g_plot_registry.register_plot("DAQ", 0x703, "RSSI", "RF Stats");
    g_plot_registry.register_plot("DAQ", 0x781, "LTE_RSSI", "LTE Signal");
}

void init_default_plot_drawers(){
    g_plot_drawers.register_drawer("Moco. Info", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Bus", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Velocity", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Phase Currents", make_line_drawer("Current (A)"));
    g_plot_drawers.register_drawer("Voltage Vector", make_line_drawer("Voltage (V)"));
    g_plot_drawers.register_drawer("Current Vector", make_line_drawer("Current (A)"));
    g_plot_drawers.register_drawer("Back EMF", make_line_drawer("Voltage (V)"));
    g_plot_drawers.register_drawer("Low Voltage Rail", make_line_drawer("Voltage (V)"));
    g_plot_drawers.register_drawer("DSP Voltage Rail", make_line_drawer("Voltage (V)"));
    g_plot_drawers.register_drawer("Precharge", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Faults", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("State", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Moco. Safe", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Moco. Status", make_line_drawer("Value"));

    g_plot_drawers.register_drawer("Trip", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("All Clear", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Contactors", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Current", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Voltage Array", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Temperature Array", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("SOC", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Watchdog", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("CAN Error", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Command", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Supplemental Voltage", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Charge", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Voltage Summary", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Temperature Summary", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Fault State", make_line_drawer("Value"));

    g_plot_drawers.register_drawer("Info", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Status", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("15V Rail", make_line_drawer("Voltage (V)"));
    g_plot_drawers.register_drawer("Reserved0A", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Temperatures", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("DSP Board Temp", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Reserved0D", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Odometer", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Slip Speed", make_line_drawer("Value"));

    g_plot_drawers.register_drawer("MPPT32 Status", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("MPPT32 Power", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("MPPT33 Status", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("MPPT33 Power", make_line_drawer("Value"));

    g_plot_drawers.register_drawer("RF Stats", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("LTE Signal", make_line_drawer("Value"));
}
