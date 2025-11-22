#include "ui.hpp"
#include "../engine/include.hpp"
#include "imgui.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cfloat>
#include <sstream>
#include <iomanip>
#include <limits>
#include "console.hpp"
#include "imgui_internal.h"
#include "implot.h"
#include "dbc.hpp"

void UI::build(){
    static bool showFps = false;
    ImGui::NewFrame();
    background();
    if(ImGui::IsKeyReleased(ImGuiKey_F3)) showFps = !showFps;
    if(showFps) fpsWindow();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBackground |
                                   ImGuiWindowFlags_NoDocking;
    if(ImGui::Begin("Debug", NULL, windowFlags)){
        if(ImGui::IsWindowFocused())
            ImGui::TextUnformatted("Some different text...");

    } ImGui::End();

    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    if(ImGui::Begin("Main", NULL, windowFlags)){
        if(ImGui::IsWindowFocused())
            ImGui::TextUnformatted("words...");

    } ImGui::End();

    ImGui::Render();
}

void UI::GenericPlot(const std::vector<double>& yAxis, const std::vector<double>& xAxis, std::string name){
    if (xAxis.size() < 2 || yAxis.size() != xAxis.size()) { return; }

    constexpr double maxTime = 5.0;
    const double windowStart = std::max(0.0, xAxis.back() - maxTime);
    auto startIt = std::lower_bound(xAxis.begin(), xAxis.end(), windowStart);
    const std::size_t startIdx = static_cast<std::size_t>(std::distance(xAxis.begin(), startIt));
    if (startIdx >= xAxis.size()) { return; }

    double currentMin = yAxis[startIdx];
    double currentMax = yAxis[startIdx];
    for (std::size_t i = startIdx; i < yAxis.size(); ++i) {
        currentMin = std::min(currentMin, yAxis[i]);
        currentMax = std::max(currentMax, yAxis[i]);
    }
    if (std::abs(currentMax - currentMin) < 1e-3) {
        double span = std::max(1.0, std::abs(currentMax));
        currentMin -= span * 0.5;
        currentMax += span * 0.5;
    }
    double pad = (currentMax - currentMin) * 0.1;
    double yMin = currentMin - pad;
    double yMax = currentMax + pad;

//    ImGui::SetNextWindowSize({600, 325});
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration;
    if(ImGui::Begin(name.c_str(), NULL, flags)){
        ImPlot::SetNextAxisLimits(ImAxis_X1, windowStart, xAxis.back(), ImPlotCond_Always);
        ImPlot::SetNextAxisLimits(ImAxis_Y1, yMin, yMax, ImPlotCond_Always);
        if(ImPlot::BeginPlot(name.c_str())){
            const double* xData = xAxis.data() + startIdx;
            const double* yData = yAxis.data() + startIdx;
            const int count = static_cast<int>(xAxis.size() - startIdx);
            ImPlot::PlotLine(name.c_str(), xData, yData, count);
            ImPlot::EndPlot();
        }
    } 
    ImGui::End();
}

void UI::GenericPlotTab(const std::vector<double>& yAxis, const std::vector<double>& xAxis, const char* name){
    if (xAxis.size() < 1 || yAxis.size() != xAxis.size() || yAxis.empty()) { return; }
    constexpr double maxTime = 5.0;
    const double windowStart = std::max(0.0, xAxis.back() - maxTime);
    auto startIt = std::lower_bound(xAxis.begin(), xAxis.end(), windowStart);
    const std::size_t startIdx = static_cast<std::size_t>(std::distance(xAxis.begin(), startIt));
    if (startIdx >= xAxis.size()) { return; }

    double currentMin = yAxis[startIdx];
    double currentMax = yAxis[startIdx];
    for (std::size_t i = startIdx; i < yAxis.size(); ++i) {
        currentMin = std::min(currentMin, yAxis[i]);
        currentMax = std::max(currentMax, yAxis[i]);
    }
    if (std::abs(currentMax - currentMin) < 1e-3) {
        double span = std::max(1.0, std::abs(currentMax));
        currentMin -= span * 0.5;
        currentMax += span * 0.5;
    }
    double pad = (currentMax - currentMin) * 0.1;
    double yMin = currentMin - pad;
    double yMax = currentMax + pad;
        ImPlot::SetNextAxisLimits(ImAxis_X1, windowStart, xAxis.back(), ImPlotCond_Always);
        ImPlot::SetNextAxisLimits(ImAxis_Y1, yMin, yMax, ImPlotCond_Always);
        std::string plotLabel = std::string(name) + "##plot";
        std::string lineLabel = std::string(name) + "##line";
        if (ImPlot::BeginPlot(plotLabel.c_str(), ImVec2(-FLT_MIN, 300.0f), ImPlotFlags_NoLegend)) {
            const double* xData = xAxis.data() + startIdx;
            const double* yData = yAxis.data() + startIdx;
            const int count = static_cast<int>(xAxis.size() - startIdx);
            ImPlot::PlotLine(lineLabel.c_str(), xData, yData, count);
            double latestY = yAxis.back();
            char valueLabel[64];
            std::snprintf(valueLabel, sizeof(valueLabel), "Most Recent: %.3f", latestY);
            ImVec2 plotPos = ImPlot::GetPlotPos();
            ImPlot::GetPlotDrawList()->AddText(ImVec2(plotPos.x + 6.0f, plotPos.y + 6.0f),
                                               IM_COL32(255, 255, 255, 255),
                                               valueLabel);
            ImPlot::EndPlot();
        }
}

void UI::debugWindowTab(){
    // Place tab-local debug widgets here; keeps them inside the tab instead of a separate window.
    ImGui::SeparatorText("Debug");
    // Existing debugWindow() remains unchanged; call here only if it renders inline content.
    // debugWindow();
}


void UI::procedural(std::vector<Plot*> plots){
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;
    ImVec2 windowSize = ImVec2(displaySize.x / 3.0, displaySize.y / 3.0);
    ImVec2 pos = ImVec2(0,0);
    for(int i = 0; i < plots.size(); i++){
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
        if(pos.x > (displaySize.x - windowSize.x + 1.0)){
            pos.x = 0.0;
            pos.y = pos.y + windowSize.y;
        }
        ImGui::SetNextWindowPos(pos);
        plots[i]->update(networkINTF);
        pos.x = pos.x + windowSize.x;
    }
}

void UI::debugWindow(){
}

void UI::basePlate(){
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;
    ImGuiViewport *viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowSize({displaySize.x, displaySize.y}, ImGuiCond_Always);
    ImGui::SetNextWindowPos({0, 0});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    if(ImGui::Begin("##base", NULL, flags)){

    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void UI::defaultWindow(std::string name){
    ImGui::SetNextWindowSize({200, 400}, ImGuiCond_Once);
    if(ImGui::Begin(name.data(), NULL, 0)){

    }
    ImGui::End();
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
                                   ImGuiWindowFlags_NoBackground |
                                   ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    // Stats window
    if (ImGui::Begin("Photon Stats", NULL, windowFlags)) {
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
                         NULL, renderSettings.frameTimeMin, renderSettings.frameTimeMax,
                         ImVec2(240, 80));
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
}

// VulkanObj rendering disabled for stability; objWindow stub left commented.

void UI::shaderWindow(VulkanShader& shader, std::string windowName){
    if(!shader.texture) {return;}
    ImGui::SetNextWindowSize(ImVec2(shader.extent.width, shader.extent.height), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = 0;//ImGuiWindowFlags_NoDecoration;
    if(ImGui::Begin(windowName.data(), NULL, flags)){
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        if(contentSize.x <= 1.0f || contentSize.y <= 1.0f){
            contentSize = ImVec2(shader.extent.width, shader.extent.height);
        }
        const float delta = 0.5f;
        if(contentSize.x > 1.0f && contentSize.y > 1.0f){
            if(std::fabs(contentSize.x - shader.extent.width) > delta ||
               std::fabs(contentSize.y - shader.extent.height) > delta){
                shader.extent.width = contentSize.x;
                shader.extent.height = contentSize.y;
                shader.dirty = true;
            }
        }
        ImVec2 drawSize(shader.extent.width, shader.extent.height);
        drawSize.x = std::max(drawSize.x, 1.0f);
        drawSize.y = std::max(drawSize.y, 1.0f);
        ImGui::Image(shader.texture, drawSize);
    }
    ImGui::End();
}

void UI::showVideoDisplay(){
    if (!videoSource.texture) { return; }
    if (ImGui::Begin("Custom Image", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImVec2 size = ImVec2(videoSource.textureSize.width, videoSource.textureSize.height);
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
        ImGui::Image(videoSource.texture, drawSize);
    }
    ImGui::End();
}

void UI::background(){
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;
    if (displaySize.x > 0.0f && displaySize.y > 0.0f) {
        const float epsilon = 0.5f;
        if (std::fabs(backgroundShader.extent.width- displaySize.x) > epsilon ||
            std::fabs(backgroundShader.extent.height - displaySize.y) > epsilon) {
            backgroundShader.extent.width = displaySize.x;
            backgroundShader.extent.height = displaySize.y;
            backgroundShader.dirty = true;
        }
    }

    if (!backgroundShader.texture) { return; }

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    if (!viewport) { return; }

    ImDrawList *drawList = ImGui::GetBackgroundDrawList(viewport);
    ImVec2 min = viewport->Pos;
    ImVec2 max = ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
    drawList->AddImage(this->backgroundShader.texture, min, max);
}

void UI::setStyle(){
    ImGuiStyle &UIstyle = ImGui::GetStyle();
    ImVec4* colors = UIstyle.Colors;

    UIstyle.WindowRounding = 12.0f;
    UIstyle.ChildRounding = 12.0f;
    UIstyle.PopupRounding = 12.0f;

    colors[ImGuiCol_WindowBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 0.5f); 
    colors[ImGuiCol_ChildBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
    colors[ImGuiCol_PopupBg] =
        ImVec4(0.05f, 0.05f, 0.05f, 0.5f);

    // Borders and separators
    colors[ImGuiCol_Border] =
        ImVec4(0.2f, 0.2f, 0.2f, 1.0f); 
    colors[ImGuiCol_Separator] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);

    // Text colors
    colors[ImGuiCol_Text] = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); 
    colors[ImGuiCol_TextDisabled] =
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f); 

    // Headers and title
    colors[ImGuiCol_Header] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f); 
    colors[ImGuiCol_HeaderHovered] =
        ImVec4(0.25f, 0.25f, 0.25f, 1.0f); 
    colors[ImGuiCol_HeaderActive] =
        ImVec4(0.3f, 0.3f, 0.3f, 1.0f); 

    colors[ImGuiCol_TitleBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.7f);

    // Buttons
    colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

    // Sliders, checks, etc.
    colors[ImGuiCol_SliderGrab] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

    colors[ImGuiCol_CheckMark] =
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

    // Frame backgrounds
    colors[ImGuiCol_FrameBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2f, 0.2f, 0.2f, 0.9f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.1f, 0.1f, 0.1f, 0.8f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);

    // Resize grips
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.2f, 0.2f, 0.2f, 0.2f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.5f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.4f, 0.4f, 0.4f, 0.7f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.2f, 0.2f, 0.2f, 0.7f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.8f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

    // Misc
    colors[ImGuiCol_PlotLines] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

    colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 1.0f, 0.9f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

    // Transparency handling
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);

    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    UIstyle.ScaleAllSizes(1.0f);
    ImPlotStyle &plotStyle = ImPlot::GetStyle();
}

void UI::tempWork(){
    static IO_State iostate;
    static Controls_Fault controls_fault;
    static Motor_Drive_Command drive_cmd;
    static Motor_Power_Command power_cmd;
    static Pedals_Raw_Voltage pedals_raw;
    
    static BPS_Current bps_current;
    static BPS_Voltage_Array voltage_arr;
    static BPS_Temperature_Array temp_arr;
    static BPS_SOC bps_soc;
    static BPS_Supplemental_Voltage supp_volt;
    static BPS_Voltage_Summary volt_sum;
    static BPS_Temperature_Summary temp_sum;
    
    iostate.updateSignals(networkINTF);
    controls_fault.updateSignals(networkINTF);
    drive_cmd.updateSignals(networkINTF);
    power_cmd.updateSignals(networkINTF);
    pedals_raw.updateSignals(networkINTF);
    voltage_arr.updateSignals(networkINTF);
    temp_arr.updateSignals(networkINTF);
    volt_sum.updateSignals(networkINTF);
    temp_sum.updateSignals(networkINTF);

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 size = vp->Size;
    ImVec2 pos  = vp->Pos;
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove 
        | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoResize; //| ImGuiWindowFlags_NoDecoration;
    ImGui::Begin("Base", NULL, flags);
    {
        ImVec2 tabOrigin = ImGui::GetCursorPos();
        float columnW = 600;
        float rowH = 300;
        float gap = 8.0f;
        float summaryH = rowH * 0.5f;

        auto offset = [&](float dx, float dy) {
          return ImVec2(tabOrigin.x + dx, tabOrigin.y + dy);
        };

        // Accel panel
        ImGui::SetCursorPos(offset(0.0f, 0.0f));
        ImGui::BeginChild("Accel", ImVec2(columnW, rowH));
        GenericPlotTab(pedals_raw.Accel_Raw, pedals_raw.time, "Accel Raw");
        ImGui::EndChild();

        // Brake panel to the right
        ImGui::SetCursorPos(offset(columnW + gap, 0.0f));
        ImGui::BeginChild("Brake", ImVec2(columnW, rowH));
        GenericPlotTab(pedals_raw.Brake_Raw, pedals_raw.time, "Brake Raw");
        ImGui::EndChild();

        // Next row
        ImGui::SetCursorPos(offset(0.0f, rowH + gap));
        ImGui::BeginChild("AccelPct", ImVec2(columnW, rowH));
        GenericPlotTab(iostate.Acceleration_Percentage, iostate.time, "Accel percentage");
        ImGui::EndChild();

        ImGui::SetCursorPos(offset(columnW + gap, rowH + gap));
        ImGui::BeginChild("BrakePct", ImVec2(columnW, rowH));
        GenericPlotTab(iostate.Brake_Percentage, iostate.time, "Brake percentage");
        ImGui::EndChild();

        const float col3X = (columnW + gap) * 2.0f;

        // Battery modules grid (row 3, spans columns 1-2)
        ImGui::SetCursorPos(offset(0.0f, (rowH + gap) * 2.0f));
        ImGui::BeginChild("ModuleGrid", ImVec2(columnW * 2.0f + gap, rowH));
        {
            // Collect latest voltage/temp per module index
            std::vector<double> latestVolt;
            std::vector<double> latestTemp;
            auto collectLatest = [](const std::vector<double>& idxVec,
                                    const std::vector<double>& valueVec,
                                    std::vector<double>& out,
                                    double scale){
                const size_t kMaxModules = 32;
                out.assign(kMaxModules, 0.0);
                for (size_t i = 0; i < idxVec.size() && i < valueVec.size(); ++i) {
                    size_t idx = static_cast<size_t>(idxVec[i]);
                    if (idx >= kMaxModules) { continue; }
                    out[idx] = valueVec[i] * scale;
                }
            };
            collectLatest(voltage_arr.Voltage_idx, voltage_arr.Voltage_Value_mV, latestVolt, 0.001); // mV -> V
            collectLatest(temp_arr.Temperature_idx, temp_arr.Temperature_Value_mC, latestTemp, 0.001); // mC -> C

            const int num_cols = 8;
            const size_t num_modules = 32;
            if (ImGui::BeginTable("ModTable", num_cols, ImGuiTableFlags_SizingFixedFit)) {
                ImGuiStyle& style = ImGui::GetStyle();
                float avail_height = ImGui::GetContentRegionAvail().y;
                // Four internal rows: each module takes ~1/4 of the row height
                float cell_height = std::max(16.0f, (avail_height / 4.0f) - style.CellPadding.y * 2.0f);
                float avail_width = ImGui::GetContentRegionAvail().x;
                float cell_width = (avail_width / num_cols) - style.CellPadding.x * 2.0f;

                for (int col = 0; col < num_cols; ++col) {
                    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, cell_width);
                }

                for (size_t pos = 0; pos < num_modules; ++pos) {
                    ImGui::TableNextColumn();
                    int row = static_cast<int>(pos / num_cols);
                    int col = static_cast<int>(pos % num_cols);
                    int moduleNum = (row % 2 == 0) ? (row * num_cols + (num_cols - col)) : (row * num_cols + col + 1);
                    size_t dataIdx = static_cast<size_t>(moduleNum - 1);

                    double volt = (dataIdx < latestVolt.size()) ? latestVolt[dataIdx] : std::numeric_limits<double>::quiet_NaN();
                    double tempC = (dataIdx < latestTemp.size()) ? latestTemp[dataIdx] : std::numeric_limits<double>::quiet_NaN();

                    // Normalize temp for color
                    double tmin = -20.0, tmax = 60.0;
                    double norm = (tempC - tmin) / (tmax - tmin);
                    if (std::isnan(norm)) norm = 0.5;
                    norm = std::clamp(norm, 0.0, 1.0);
                    ImVec4 border_col = ImPlot::SampleColormap(static_cast<float>(norm), ImPlotColormap_Viridis);

                    std::ostringstream oss;
                    oss << "Module " << std::setw(2) << std::setfill('0') << moduleNum << "\n";
                    oss << std::fixed << std::setprecision(2);
                    if (!std::isnan(volt)) oss << volt << " V\n"; else oss << "-- V\n";
                    if (!std::isnan(tempC)) oss << tempC << " C"; else oss << "-- C";
                    std::string label = oss.str();

                    ImVec4 bg = style.Colors[ImGuiCol_FrameBg];
                    ImGui::PushStyleColor(ImGuiCol_Button, bg);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg);
                    ImGui::PushStyleColor(ImGuiCol_Border, border_col);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

                    ImGui::Button(label.c_str(), ImVec2(cell_width, cell_height));

                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(4);
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();

        // Motor velocity setpoint (row 1, col 3)
        ImGui::SetCursorPos(offset(col3X, 0.0f));
        ImGui::BeginChild("MotorVelocity", ImVec2(columnW, rowH));
        GenericPlotTab(drive_cmd.Motor_Velocity_Setpoint, drive_cmd.time, "Motor velocity setpoint");
        ImGui::EndChild();

        // Motor current setpoint (row 2, col 3)
        ImGui::SetCursorPos(offset(col3X, rowH + gap));
        ImGui::BeginChild("MotorCurrent", ImVec2(columnW, rowH));
        GenericPlotTab(drive_cmd.Motor_Current_Setpoint, drive_cmd.time, "Motor current setpoint");
        ImGui::EndChild();

        // Motor power setpoint (row 3, col 3)
        ImGui::SetCursorPos(offset(col3X, (rowH + gap) * 2.0f));
        ImGui::BeginChild("MotorPower", ImVec2(columnW, rowH));
        GenericPlotTab(power_cmd.Motor_Power_Setpoint, power_cmd.time, "Motor power setpoint");
        ImGui::EndChild();

        // BPS summaries row (spans all columns, holds summaries + faults)
        ImGui::SetCursorPos(offset(0.0f, (rowH + gap) * 3.0f));
        ImGui::BeginChild("BpsSummary", ImVec2(columnW * 3.0f + gap * 2.0f, summaryH));
        {
            auto lastOrNa = [](const std::vector<double>& v) -> double {
                if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
                return v.back();
            };
            double packV = lastOrNa(volt_sum.Pack_Voltage_mV) * 0.001; // mV -> V
            double vRange = lastOrNa(volt_sum.Voltage_Range_mV) * 0.001; // mV -> V
            double packTs = lastOrNa(volt_sum.Voltage_Timestamp_ms);
            double avgTemp = lastOrNa(temp_sum.Average_Temp_mC) * 0.001; // mC -> C
            double tempRange = lastOrNa(temp_sum.Temperature_Range_mC) * 0.001; // mC -> C
            double tempTs = lastOrNa(temp_sum.Temperature_Timestamp_ms);

            auto fmt = [](double v, int prec) {
                if (std::isnan(v)) return std::string("--");
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(prec) << v;
                return oss.str();
            };

            std::string packStr = fmt(packV, 2);
            std::string vRangeStr = fmt(vRange, 2);
            std::string packTsStr = fmt(packTs, 0);
            std::string avgTempStr = fmt(avgTemp, 2);
            std::string tempRangeStr = fmt(tempRange, 2);
            std::string tempTsStr = fmt(tempTs, 0);

            const int cols = 9; // Volt label + 3 vals + sep + Temp label + 3 vals
            float availW = ImGui::GetContentRegionAvail().x;
            float baseW = (availW - 20.0f) / cols; // leave a bit for separator
            if (ImGui::BeginTable("BpsSummaryTable", cols, ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, baseW); // Voltage label
                ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, baseW);
                ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, baseW);
                ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, baseW);
                ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, 20.0f); // separator
                ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, baseW); // Temp label
                ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, baseW);
                ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, baseW);
                ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, baseW);

                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted("BPS Voltage:");
                ImGui::TableNextColumn(); ImGui::Text("Pack: %s V", packStr.c_str());
                ImGui::TableNextColumn(); ImGui::Text("Range: %s V", vRangeStr.c_str());
                ImGui::TableNextColumn(); ImGui::Text("TS: %s ms", packTsStr.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted("|");
                ImGui::TableNextColumn(); ImGui::TextUnformatted("BPS Temp:");
                ImGui::TableNextColumn(); ImGui::Text("Avg: %s C", avgTempStr.c_str());
                ImGui::TableNextColumn(); ImGui::Text("Range: %s C", tempRangeStr.c_str());
                ImGui::TableNextColumn(); ImGui::Text("TS: %s ms", tempTsStr.c_str());

                ImGui::EndTable();
            }

            // Controls fault flags directly below summary line
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            auto lastBit = [](const std::vector<double>& v) -> int {
                if (v.empty()) return -1;
                return static_cast<int>(v.back());
            };
            int fault = lastBit(controls_fault.Controls_Fault_Flag);
            int mcf   = lastBit(controls_fault.Motor_Controller_Fault);
            int bpsf  = lastBit(controls_fault.BPS_Fault);
            int pedf  = lastBit(controls_fault.Pedals_Fault);
            int carf  = lastBit(controls_fault.CarCAN_Fault);
            int intf  = lastBit(controls_fault.Internal_Controls_Fault);
            int osf   = lastBit(controls_fault.OS_Fault);
            int lakf  = lastBit(controls_fault.Lakshay_Fault);

            struct FaultRow { const char* label; int bit; };
            FaultRow faults[] = {
                {"Fault", fault}, {"MC Fault", mcf}, {"BPS Fault", bpsf}, {"Pedals Fault", pedf},
                {"CarCAN Fault", carf}, {"Internal", intf}, {"OS", osf}, {"Lakshay", lakf}
            };

            const int faultCols = 8;
            float fAvailW = ImGui::GetContentRegionAvail().x;
            float fCellW = (fAvailW - 16.0f) / faultCols;
            float radius = ImGui::GetTextLineHeight() * 0.4f;
            if (ImGui::BeginTable("ControlsFaultTable", faultCols, ImGuiTableFlags_SizingFixedFit)) {
                for (int c = 0; c < faultCols; ++c) {
                    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, fCellW);
                }
                ImGui::TableNextRow();

                auto colorFor = [](int bit) -> ImU32 {
                    if (bit < 0) return IM_COL32(128, 128, 128, 255);
                    return (bit == 0) ? IM_COL32(50, 200, 120, 255) : IM_COL32(230, 70, 70, 255);
                };

                for (const auto& f : faults) {
                    ImGui::TableNextColumn();
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f));
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius, colorFor(f.bit));
                    ImGui::SameLine();
                    ImGui::TextUnformatted(f.label);
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }

    ImGui::End();
}
