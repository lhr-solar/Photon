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
    if(cmdShowPopup){ popupFocused = popupWindow(); }
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
                        childFocused = popupWide(msg.signals[idx], {ImGui::GetWindowWidth() + 10,0});
                    }
                }
            ImGui::EndListBox();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    return (focused || childFocused);
}

bool UI::popupWide(const CanSignal& sig, ImVec2 pos){
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
        ImGui::Text("This text is based on the selected signal: %s", sig.name.data());
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
