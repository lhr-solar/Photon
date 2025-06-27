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

void init_default_plot_drawers(){
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
}
