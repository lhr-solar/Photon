#include "ui.hpp"
#include "../engine/include.hpp"
#include <chrono>
#include <cmath>
#include <algorithm>

void UI::build(){
    ImGui::NewFrame();

    customBackground();
    ImPlot::ShowDemoWindow();
    ImPlot3D::ShowDemoWindow();
    fpsWindow();
    customShaderWindow();
    showVideoDisplay();
    networkSamplePlot();

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
