#include "imgui.h"
#include "implot.h"
#include "plot.hpp"
#include "../network/network.hpp"

Plot::Plot(int canID, const char* windowName, const char* plotName)
    : data{std::vector<double>{0.0}, std::vector<double>{0.0}},
      canID(canID),
      windowName(windowName),
      plotName(plotName) {}

void Plot::update(Network* networkSource){
    uint64_t val = 0;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    double maxTime = 5.0;
    auto prune = [maxTime](std::vector<std::vector<double>>& series){
        while((series[0].size() > 1) && ((series[0].back() - series[0].front()) > maxTime)){
            series[0].erase(series[0].begin());
            series[1].erase(series[1].begin());
        }
    };
    const bool hasNew = networkSource->readSample(canID, val);
    prune(data);
    data[0].push_back(data[0].back() + deltaTime);
    if (!hasNew && !data[1].empty()) {
        data[1].push_back(data[1].back());
    } else {
        data[1].push_back((double)val);
    }

    double currentMin = data[1].front();
    double currentMax = data[1].front();
    for (double sample : data[1]) {
        currentMin = std::min(currentMin, sample);
        currentMax = std::max(currentMax, sample);
    }

    if (currentMax - currentMin < 1e-3) {
        double span = std::max(1.0, std::abs(currentMax));
        currentMin -= span * 0.5;
        currentMax += span * 0.5;
    }

    double padding = (currentMax - currentMin) * 0.1;
    minValue = currentMin - padding;
    maxValue = currentMax + padding;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoDecoration;
    if(ImGui::Begin(windowName.data(), NULL, flags)){
        ImPlot::SetNextAxisLimits(ImAxis_X1, std::max(0.0, data[0].back() - 5.0), data[0].back(), ImPlotCond_Always);
        ImPlot::SetNextAxisLimits(ImAxis_Y1, minValue, maxValue, ImPlotCond_Always);
        if(ImPlot::BeginPlot(plotName.data())){
            ImPlot::PlotLine(plotName.data(), data[0].data(), data[1].data(), data[0].size());
            ImPlot::EndPlot();
        }
    } 
    ImGui::End();
}
