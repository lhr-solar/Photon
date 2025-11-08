#include "ui.hpp"
#include "../engine/include.hpp"
#include "imgui.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include "console.hpp"
#include "implot.h"

void UI::build(){
    ImGui::NewFrame();
    static bool flag = true;
    static Console console;

    fpsWindow();
    customBackground();
//    customShaderWindow();
//    console.Draw("Console", &flag);
//    networkSamplePlot();
    /*
    imuWindow();

    static std::vector<std::vector<double>> accX(2, std::vector<double>(1,0));
    static std::vector<ImVec2> fx1;
    defaultPlot(accX, fx1, 0x403, "Acceleration X", "AccX");

    static std::vector<std::vector<double>> accY(2, std::vector<double>(1,0));
    static std::vector<ImVec2> fx2;
    defaultPlot(accY, fx2, 0x404, "Acceleration Y", "AccY");

    static std::vector<std::vector<double>> accZ(2, std::vector<double>(1,0));
    static std::vector<ImVec2> fx3;
    defaultPlot(accZ, fx2, 0x405, "Acceleration Z", "AccZ");
    */

//    ImPlot::ShowDemoWindow();
//    ImPlot3D::ShowDemoWindow();
//    ImGui::ShowDemoWindow();

    showVideoDisplay();
    ImGui::Render();
}

void plotGlow(std::vector<double> xCoords, std::vector<double> yCoords, std::vector<ImVec2>& glowPts){
    const int glowPasses = 2;
    glowPts.resize(xCoords.size());
    for (size_t i = 0; i < xCoords.size(); ++i)
        glowPts[i] = ImPlot::PlotToPixels(ImPlotPoint(xCoords[i], yCoords[i]));

    ImPlot::PushPlotClipRect();
    ImDrawList* dl = ImPlot::GetPlotDrawList();
    for (int pass = glowPasses - 1; pass >= 0; --pass) {
        float thickness = 3.0f + pass * 2.5f;
        float alpha     = 0.12f - pass * 0.02f;
        ImU32 col = ImGui::GetColorU32(ImVec4(0.8f, 0.8f, 0.8f, alpha));
        dl->AddPolyline(glowPts.data(), (int)glowPts.size(), col, ImDrawFlags_None, thickness);
    } ImPlot::PopPlotClipRect();
}

void UI::defaultPlot(std::vector<std::vector<double>>& data, std::vector<ImVec2>& fx, int canID, const char* windowName, const char* plotName){
    int64_t val;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    double maxTime = 5.0;
    auto prune = [maxTime](std::vector<std::vector<double>>& series){
        while((series[0].size() > 1) && ((series[0].back() - series[0].front()) > maxTime)){
            series[0].erase(series[0].begin());
            series[1].erase(series[1].begin());
        }
    };
    networkINTF->readSample(canID, val);
    prune(data);
    data[0].push_back(data[0].back() + deltaTime);
    data[1].push_back((double)val);

    ImGuiWindowFlags flags = 0;
    ImGui::SetNextWindowSize(ImVec2(600.0f, 350.0f), ImGuiCond_FirstUseEver);
    if(ImGui::Begin(windowName, NULL, flags)){
        ImPlot::SetNextAxisLimits(ImAxis_X1, std::max(0.0, data[0].back() - 5.0), data[0].back(), ImPlotCond_Always);
        ImPlot::SetNextAxisLimits(ImAxis_Y1, -3000, 3000, ImPlotCond_Always);
        if(ImPlot::BeginPlot(plotName)){
            ImPlot::PlotLine(plotName, data[0].data(), data[1].data(), data[0].size());
            //plotGlow(data[0], data[1], fx);
            ImPlot::EndPlot();
        }
    } 
    ImGui::End();
}

void UI::imuWindow(){
    static std::vector<std::vector<double>> gyrX(2, std::vector<double>(1,0));
    static std::vector<std::vector<double>> gyrY(2, std::vector<double>(1,0));
    static std::vector<std::vector<double>> gyrZ(2, std::vector<double>(1,0));

    int64_t val;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    double maxTime = 5.0;

    auto prune = [maxTime](std::vector<std::vector<double>>& series){
        while((series[0].size() > 1) && ((series[0].back() - series[0].front()) > maxTime)){
            series[0].erase(series[0].begin());
            series[1].erase(series[1].begin());
        }
    };

    networkINTF->readSample(0x0400, val);
    prune(gyrX);
    gyrX[0].push_back(gyrX[0].back() + deltaTime);
    gyrX[1].push_back((double)val);

    networkINTF->readSample(0x0401, val);
    prune(gyrY);
    gyrY[0].push_back(gyrY[0].back() + deltaTime);
    gyrY[1].push_back((double)val);

    networkINTF->readSample(0x0402, val);
    prune(gyrZ);
    gyrZ[0].push_back(gyrZ[0].back() + deltaTime);
    gyrZ[1].push_back((double)val);

    ImGuiWindowFlags flags = 0;
    ImGui::SetNextWindowSize(ImVec2(600.0f, 350.0f), ImGuiCond_FirstUseEver);
    if(ImGui::Begin("IMU Data", NULL, flags)){

        ImPlot::SetNextAxisLimits(ImAxis_X1, std::max(0.0, gyrX[0].back() - 5.0), gyrX[0].back(), ImPlotCond_Always);
        ImPlot::SetNextAxisLimits(ImAxis_Y1, -300000, 300000, ImPlotCond_Always);
        if(ImPlot::BeginPlot("##Gyro X")){
            //static std::vector<ImVec2> glowPts;
            ImPlot::PlotLine("Gyro X", gyrX[0].data(), gyrX[1].data(), gyrX[0].size());
            ImPlot::PlotLine("Gyro Y", gyrY[0].data(), gyrY[1].data(), gyrY[0].size());
            ImPlot::PlotLine("Gyro Z", gyrZ[0].data(), gyrZ[1].data(), gyrZ[0].size());
            //plotGlow(gyrX[0], gyrX[1], glowPts);
            //plotGlow(gyrY[0], gyrY[1], glowPts);
            //plotGlow(gyrZ[0], gyrZ[1], glowPts);
            ImPlot::EndPlot();
        }

        /*
        ImPlot::SetNextAxisLimits(ImAxis_X1, std::max(0.0, gyrY[0].back() - 5.0), gyrY[0].back(), ImPlotCond_Always);
        ImPlot::SetNextAxisLimits(ImAxis_Y1, -300000, 300000, ImPlotCond_Always);
        if(ImPlot::BeginPlot("##Gyro Y")){
            ImPlot::PlotLine("Gyro Y", gyrY[0].data(), gyrY[1].data(), gyrY[0].size());
            ImPlot::EndPlot();
        }

        ImPlot::SetNextAxisLimits(ImAxis_X1, std::max(0.0, gyrY[0].back() - 5.0), gyrY[0].back(), ImPlotCond_Always);
        ImPlot::SetNextAxisLimits(ImAxis_Y1, -300000, 300000, ImPlotCond_Always);
        if(ImPlot::BeginPlot("##Gyro Z")){
            ImPlot::PlotLine("Gyro Z", gyrZ[0].data(), gyrZ[1].data(), gyrZ[0].size());
            ImPlot::EndPlot();
        }
        */
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
    if (!accretionShader.texture) { return; }

    ImGui::SetNextWindowSize(ImVec2(accretionShader.extent.width, accretionShader.extent.height), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Custom Shader")) {
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        if (contentSize.x <= 1.0f || contentSize.y <= 1.0f) {
            contentSize = ImVec2(accretionShader.extent.width, accretionShader.extent.height);
        }

        const float epsilon = 0.5f;
        if (contentSize.x > 1.0f && contentSize.y > 1.0f) {
            if (std::fabs(contentSize.x - accretionShader.extent.width) > epsilon ||
                std::fabs(contentSize.y - accretionShader.extent.height) > epsilon) {
                accretionShader.extent.width = contentSize.x;
                accretionShader.extent.height = contentSize.y;
                accretionShader.dirty = true;
            }
        }

        ImVec2 drawSize(accretionShader.extent.width, accretionShader.extent.height);
        drawSize.x = std::max(drawSize.x, 1.0f);
        drawSize.y = std::max(drawSize.y, 1.0f);
        ImGui::Image(accretionShader.texture, drawSize);
    }
    ImGui::End();
}

void UI::showVideoDisplay(){
    if (!videoSource.texture) { return; }
    if (ImGui::Begin("Custom Image", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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

    int64_t rawValue = 0;
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

void UI::customBackground(){
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
    ImGuiStyle UIstyle = ImGui::GetStyle();
    // pointer to store style, do not modify directly
    ImGuiStyle &setStyle = ImGui::GetStyle();     

    ImVec4* colors = UIstyle.Colors;
    colors[ImGuiCol_WindowBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 0.9f); 
    colors[ImGuiCol_ChildBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 0.9f);
    colors[ImGuiCol_PopupBg] =
        ImVec4(0.05f, 0.05f, 0.05f, 0.9f);

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
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);

    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    setStyle = UIstyle;
    setStyle.ScaleAllSizes(1.0f);
}
