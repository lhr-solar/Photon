#include "ui.hpp"
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
