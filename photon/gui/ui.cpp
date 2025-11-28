#include "ui.hpp"
#include "../engine/include.hpp"
#include "imgui.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cfloat>
#include <cwctype>
#include <sstream>
#include <iomanip>
#include <limits>
#include "console.hpp"
#include "imgui_internal.h"
#include "implot.h"
#include "../network/dbc.hpp"

void UI::build(){
    static bool showFps = false;
    ImGui::NewFrame();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBackground |
                                   ImGuiWindowFlags_NoDocking;
    background();
    for (auto& [id, msg] : networkINTF->canStore.canMessages) msg.updateMessage(networkINTF);
    ImGui::SetNextWindowPos(vp->Pos); ImGui::SetNextWindowSize(vp->Size);
    if(ImGui::Begin("Debug", NULL, windowFlags)){
        if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)){
            ImGui::TextUnformatted("This is the Debug window...");
        }
    } ImGui::End();
    ImGui::SetNextWindowPos(vp->Pos); ImGui::SetNextWindowSize(vp->Size);
    if(ImGui::Begin("Main", NULL, windowFlags)){
        if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)){
            ImGui::TextUnformatted("This is the Main window...");
        }
    } ImGui::End();

    // a function with a sinister soul
    cmdPrompt();

    if(ImGui::IsKeyReleased(ImGuiKey_F3)) showFps = !showFps;
    if(showFps) fpsWindow();
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

void UI::genericInlinePlot(const std::vector<double>& xAxis, const std::vector<double>& yAxis, const char* name){
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

    ImPlot::SetNextAxisLimits(ImAxis_X1, windowStart, xAxis.back(), ImPlotCond_Always);
    ImPlot::SetNextAxisLimits(ImAxis_Y1, yMin, yMax, ImPlotCond_Always);
    std::string plotLabel = std::string(name) + "##inlinePlot";
    if (ImPlot::BeginPlot(plotLabel.c_str(), ImVec2(-FLT_MIN, 200.0f), ImPlotFlags_NoLegend)) {
        const double* xData = xAxis.data() + startIdx;
        const double* yData = yAxis.data() + startIdx;
        const int count = static_cast<int>(xAxis.size() - startIdx);
        ImPlot::SetNextLineStyle({1.0, 1.0, 1.0, 1.0});
        ImPlot::PlotLine(name, xData, yData, count);
        ImPlot::EndPlot();
    }
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

void UI::basePlate(){
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;
    ImGuiViewport *viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowSize({displaySize.x, displaySize.y}, ImGuiCond_Always);
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowBgAlpha(0.0);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    if(ImGui::Begin("##base", NULL, flags)){

    }
    ImGui::End();
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
        ImGui::Text("Device Name: %s", deviceName[0] ? deviceName : "Unknown");
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

void UI::cmdPrompt(){
    if(!cmdOpen && ImGui::IsKeyPressed(ImGuiKey_Slash)){
        cmdOpen = true;
        cmdFF = true;
        cmdBuffer[0] = '\0';
        cmdResults.clear();
        cmdSelected = -1;
        cmdShowPopup = false;
    }

    if(!cmdOpen && !cmdShowPopup) { return; }

    if(ImGui::IsKeyPressed(ImGuiKey_Escape)){
        cmdOpen = false;
        cmdShowPopup = false;
        return;
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 center = vp ? vp->GetCenter() : ImVec2(0, 0);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav;

    bool windowFocused = false;
    bool inputActive = false;
    bool hidePrompt = false;

    if(cmdOpen){
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
        ImGui::SetNextWindowBgAlpha(0.25);
        if(ImGui::Begin("CommandPrompt", nullptr, flags)){
            ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
            if(cmdFF){ ImGui::SetKeyboardFocusHere(); }
            bool submitted = ImGui::InputText("##cmdInput", cmdBuffer, IM_ARRAYSIZE(cmdBuffer), inputFlags);
            if(cmdFF){ cmdFF = false; }
            search();
            windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            inputActive = ImGui::IsItemActive();

            const int resultCount = static_cast<int>(cmdResults.size());
            if(cmdResults.size()>0){
                if(cmdSelected < 0) cmdSelected = 0;
                if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)) cmdSelected = (cmdSelected + 1) % resultCount;
                if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)) cmdSelected = (cmdSelected - 1 + resultCount) % resultCount;
                if(ImGui::IsKeyPressed(ImGuiKey_Tab)) cmdSelected = (cmdSelected + 1) % resultCount;

                float rowHeight = ImGui::GetTextLineHeightWithSpacing();
                ImVec2 listSize(ImGui::GetContentRegionAvail().x, rowHeight * resultCount + ImGui::GetStyle().FramePadding.y);
                if(ImGui::BeginListBox("##cmdResults", listSize)){
                    for(int i = 0; i < resultCount; i++){
                        if(ImGui::Selectable(cmdResults[i].name.data(), i==cmdSelected)){
                            cmdSelected = i;
                            activeCmdResult = cmdResults[i];
                            cmdShowPopup = true;
                            hidePrompt = true;
                        }
                    }
                    ImGui::EndListBox();
                }
                bool activateSelection = (submitted || ImGui::IsKeyPressed(ImGuiKey_Enter) 
                                                    || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
                if(activateSelection && cmdSelected >= 0 && cmdSelected < resultCount){
                    activeCmdResult = cmdResults[cmdSelected];
                    cmdShowPopup = true;
                    hidePrompt = true;
                }
            } else { cmdSelected = -1; }
        } ImGui::End();
        ImGui::PopStyleColor(2);
    }
    bool popupFocused = false;
    if(cmdShowPopup){ popupFocused = popupWindow(); } // this is what you are looking for
    if(hidePrompt){ cmdOpen = false; }
    if(!windowFocused && !inputActive && !popupFocused){
        cmdOpen = false;
        cmdShowPopup = false;
    }
}

bool UI::popupWindow(){
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | 
                             ImGuiWindowFlags_NoSavedSettings  | 
                             ImGuiWindowFlags_NoTitleBar | 
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoMove;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
    ImGui::SetNextWindowBgAlpha(0.75);

    bool focused = false;
    bool childFocused = false;
    static int selected = 0;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
    const CanMessage& msg = networkINTF->canStore.canMessages[activeCmdResult.canID];
    if (msg.signals.empty()) {
        ImGui::Text("No signals available");
        ImGui::PopStyleColor(2);
        return focused;
    }
    selected = std::clamp(selected, 0, static_cast<int>(msg.signals.size()) - 1);
    if(ImGui::Begin("Command Result", &cmdShowPopup, flags)){
        focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGui::Text("Message Name: %s", msg.name.c_str());
        ImGui::Text("CanID: %#04x", msg.canId);
        ImGui::Text("DLC: %d", msg.dlc);
        ImGui::Text("Transmitter: %s", msg.transmitter.c_str());
        ImGui::Text("Data Rate: %.0f B/s", msg.dataRate);
        ImGui::Text("Storage Size: %.3f MiB", msg.storageSize);
        ImGui::Text("Bandwidth Percentage: %.3f", msg.bandwidthPercentage * 100.0);
        ImGui::Text("Time Since Last Update: %.3lf (s)", (long long)msg.timeSinceUpdate.count()/1000.0);
        float rowHeight = ImGui::GetTextLineHeightWithSpacing() + 4.0;
        ImVec2 listSize(ImGui::GetContentRegionAvail().x, rowHeight * msg.signals.size() + ImGui::GetStyle().FramePadding.y);
        ImGui::Separator();
        if(ImGui::BeginListBox("##popupList", listSize)){
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                for(size_t idx = 0; idx < msg.signals.size(); ++idx){
                    bool isSelected = (idx == selected);
                    if (ImGui::Selectable(msg.signals[idx].name.c_str(), isSelected, ImGuiSelectableFlags_SelectOnNav))
                        selected = (int)idx;
                    if (ImGui::IsItemFocused()) selected = static_cast<int>(idx);
                    if (isSelected){
                        ImGui::SetItemDefaultFocus();
                        // I lied.
                        // it is actually this guy
                        childFocused = popupWide(msg.signals[idx], msg.time, {ImGui::GetWindowWidth() + 20,0});
                    }
                }
            ImGui::EndListBox();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    return (focused || childFocused);
}

bool UI::popupWide(const CanSignal& sig, const std::vector<double>& time, ImVec2 pos){
    bool focused = false;
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | 
                             ImGuiWindowFlags_NoSavedSettings  | 
                             ImGuiWindowFlags_NoTitleBar | 
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
    ImGui::SetNextWindowBgAlpha(0.75);
    ImGui::SetNextWindowPos(pos);
    if(ImGui::Begin((sig.name + "wide##").data(), NULL, flags)){
        focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGui::Text("Name: %s", sig.name.c_str());
        ImGui::SameLine();
        ImGui::Text("Start Bit: %d", sig.startBit);
        ImGui::SameLine();
        ImGui::Text("Length: %d", sig.length);
        ImGui::SameLine();
        ImGui::Text("Endianness: %d", sig.endianness);
        ImGui::SameLine();
        ImGui::Text("Signed: %s", sig.isSigned ? "true" : "false");
        ImGui::SameLine();
        ImGui::Text("Scale: %.6f", sig.scale);
        ImGui::SameLine();
        ImGui::Text("Offset: %.6f", sig.offset);

        ImGui::Text("Min: %.6f", sig.min);
        ImGui::SameLine();
        ImGui::Text("Max: %.6f", sig.max);
        ImGui::SameLine();
        ImGui::Text("Unit: %s", sig.unit.c_str());
        ImGui::SameLine();
        ImGui::Text("Receiver: %s", sig.receiver.c_str());
        ImGui::SameLine();
        ImGui::Text("Last Mutated: %.3f s ago",

        std::chrono::duration<double>(std::chrono::system_clock::now() - sig.lastTimeMutated).count());
        genericInlinePlot(time, sig.data, sig.name.c_str());
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    return focused;
}

int levenshtein(const std::string& a, const std::string& b) {
    const int n = a.size();
    const int m = b.size();

    std::vector<int> prev(m+1), cur(m+1);
    for (int j = 0; j <= m; j++) prev[j] = j;
    for (int i = 1; i <= n; i++) {
        cur[0] = i;
        for(int j = 1; j <= m; j++){
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            cur[j] = std::min({
                prev[j] + 1,
                cur[j-1] + 1,
                prev[j-1] + cost
            });
        }
        prev.swap(cur);
    }
    return prev[m];
}

int distance(std::string a, std::string b) {
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);

    if (a.size() >= b.size())
        return levenshtein(a, b);

    int best = INT_MAX;
    for (size_t i = 0; i + a.size() <= b.size(); i++) {
        int d = levenshtein(a, b.substr(i, a.size()));
        if (d < best) best = d;
    }
    return best;
}

void UI::search(){
    cmdResults.clear();
    if (cmdBuffer[0] == '\0') {
        cmdSelected = -1;
        return;
    }

    std::vector<CmdResult> results;
    results.reserve(networkINTF->canStore.canMessages.size());

    for (auto& [id, msg] : networkINTF->canStore.canMessages) {
        int d = distance(cmdBuffer, msg.name);
        results.emplace_back(msg.name, d, msg.canId);
    }

    size_t limit = std::min<size_t>(5, results.size());
    if (limit == 0) {
        cmdSelected = -1;
        return;
    }

    std::partial_sort(
        results.begin(),
        results.begin() + limit,
        results.end(),
        [](auto& a, auto& b) { return a.distance < b.distance; });

    cmdResults.reserve(limit);
    for (size_t i = 0; i < limit; i++) {
        cmdResults.push_back({results[i].name, results[i].distance, results[i].canID});
    }

    if (cmdSelected >= static_cast<int>(cmdResults.size())) {
        cmdSelected = cmdResults.empty() ? -1 : 0;
    } else if (cmdSelected == -1 && !cmdResults.empty()) {
        cmdSelected = 0;
    }
}

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
    static float alpha = 80;
    drawList->AddRectFilled(min, max, IM_COL32(0,0,0,alpha));
}

void UI::setStyle(){
    ImGuiStyle &UIstyle = ImGui::GetStyle();
    ImVec4* colors = UIstyle.Colors;

    UIstyle.WindowRounding = 12.0f;
    UIstyle.ChildRounding = 12.0f;
    UIstyle.PopupRounding = 12.0f;
    UIstyle.PopupBorderSize = UIstyle.WindowBorderSize;
    UIstyle.PopupRounding = UIstyle.WindowRounding;

    colors[ImGuiCol_WindowBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 0.5f); 
    colors[ImGuiCol_ChildBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
    colors[ImGuiCol_PopupBg] =
        colors[ImGuiCol_WindowBg];

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
    // Transparent plot backgrounds.
    plotStyle.Colors[ImPlotCol_FrameBg] = ImVec4(0, 0, 0, 0.0f);
    plotStyle.Colors[ImPlotCol_PlotBg] = ImVec4(0, 0, 0, 0.0f);
    plotStyle.Colors[ImPlotCol_PlotBorder] = ImVec4(0, 0, 0, 0.0f);
    plotStyle.Colors[ImPlotCol_LegendBg] = ImVec4(0, 0, 0, 0.0f);
    plotStyle.Colors[ImPlotCol_LegendBorder] = ImVec4(0, 0, 0, 0.0f);
}
