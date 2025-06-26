#include "signal_routing.hpp"
#include <implot.h>

DbcPlotRegistry g_plot_registry;
PlotDrawerRegistry g_plot_drawers;

PlotDrawerRegistry::PlotDrawerRegistry() {
    default_drawer = [](const std::string& name,
                        const std::vector<PlotSignal>& signals,
                        const SignalDataMap& times,
                        const SignalDataMap& values){
        if(ImPlot::BeginPlot(name.c_str())){
            ImPlot::SetupAxes("Time", "Value", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
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

void init_default_plot_registry() {
    /* Controls mappings */
    g_plot_registry.register_plot("Controls", 0x240, "Device_Serial_Number", "Moco. Info");
    g_plot_registry.register_plot("Controls", 0x240, "Prohelion_ID", "Moco. Info");

    g_plot_registry.register_plot("Controls", 0x242, "Bus_Current", "Bus");
    g_plot_registry.register_plot("Controls", 0x242, "Bus_Voltage", "Bus");

    g_plot_registry.register_plot("Controls", 0x243, "Vehicle_Velocity", "Velocity");
    g_plot_registry.register_plot("Controls", 0x243, "Motor_Angular_Frequency", "Velocity");

    g_plot_registry.register_plot("Controls", 0x244, "Phase_C_Current", "Phase Currents");
    g_plot_registry.register_plot("Controls", 0x244, "Phase_B_Current", "Phase Currents");

    g_plot_registry.register_plot("Controls", 0x245, "Vd", "Voltage Vector");
    g_plot_registry.register_plot("Controls", 0x245, "Vq", "Voltage Vector");
    g_plot_registry.register_plot("Controls", 0x246, "Id", "Current Vector");
    g_plot_registry.register_plot("Controls", 0x246, "Iq", "Current Vector");

    g_plot_registry.register_plot("Controls", 0x247, "Real_Component", "Back EMF");
    g_plot_registry.register_plot("Controls", 0x247, "Peak_Phase_To_Neutral_Voltage", "Back EMF");

    g_plot_registry.register_plot("Controls", 0x248, "Actual_Voltage", "Low Voltage Rail");
    g_plot_registry.register_plot("Controls", 0x248, "Reserved", "Low Voltage Rail");

    g_plot_registry.register_plot("Controls", 0x249, "Actual_Voltage_3_3V", "DSP Voltage Rail");
    g_plot_registry.register_plot("Controls", 0x249, "Actual_Voltage_1_9V", "DSP Voltage Rail");

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

    //g_plot_registry.register_plot("Controls", 0x584, "Motor_Safe", "Faults");
}    

static PlotDrawFn make_line_drawer(const char* y_label) {
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
                ImPlot::PlotLine(sig.second.c_str(), itx->second.data(), ity->second.data(), ity->second.size());
            }
            ImPlot::EndPlot();
        }
    };
}

void init_default_plot_drawers(){
    g_plot_drawers.register_drawer("Bus", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Velocity", make_line_drawer("Value"));
    g_plot_drawers.register_drawer("Phase Currents", make_line_drawer("Current (A)"));
    g_plot_drawers.register_drawer("Voltage Vector", make_line_drawer("Voltage (V)"));
    g_plot_drawers.register_drawer("Current Vector", make_line_drawer("Current (A)"));
    g_plot_drawers.register_drawer("Back EMF", make_line_drawer("Voltage (V)"));
    g_plot_drawers.register_drawer("Low Voltage Rail", make_line_drawer("Voltage (V)"));
    g_plot_drawers.register_drawer("DSP Voltage Rail", make_line_drawer("Voltage (V)"));
}
