#include "gui.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "imgui.h"
#include "implot.h"
#include "implot3d.h"

void GUI::buildUI(){
    ImGui::NewFrame();
    dockspace();
    ImGui::ShowDemoWindow();
    ImPlot::ShowDemoWindow();
    ImPlot3D::ShowDemoWindow();
    backgroundWindow();
    gltfWindow();
    ImGui::Render();
};

void GUI::dockspace(){
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 dockspacePos(
        viewport->WorkPos.x + viewport->WorkSize.x * 0.2f,
        viewport->WorkPos.y);
    const ImVec2 dockspaceSize(
        viewport->WorkSize.x * 0.8f,
        viewport->WorkSize.y);
    ImGui::SetNextWindowPos(dockspacePos);
    ImGui::SetNextWindowSize(dockspaceSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    window_flags |= ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if(ImGui::Begin("Window with a DockSpace", nullptr, window_flags)){
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    }ImGui::End();
    ImGui::PopStyleVar(3);
}

VkExtent2D quantizeContentExtent(ImVec2 contentSize, VkExtent2D fallback) {
    if (contentSize.x <= 1.0f || contentSize.y <= 1.0f) return fallback;
    const uint32_t width = std::max(1u, static_cast<uint32_t>(std::lround(contentSize.x)));
    const uint32_t height = std::max(1u, static_cast<uint32_t>(std::lround(contentSize.y)));
    return {width, height};
}

void GUI::backgroundWindow(){
    if(!backgroundShader.initialized || backgroundShader.frames.empty() || backgroundShader.frameIndex == nullptr) return;
    shaderFrame& frame = backgroundShader.frames[*backgroundShader.frameIndex];
    ImGui::SetNextWindowSize(ImVec2(frame.extent.width, frame.extent.height), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration;
    if(ImGui::Begin("background", NULL, 0)){
        const VkExtent2D nextExtent = quantizeContentExtent(ImGui::GetContentRegionAvail(), frame.extent);
        if (nextExtent.width != frame.extent.width || nextExtent.height != frame.extent.height) {
            frame.extent = nextExtent;
            backgroundShader.dirty = true;
        }
        ImVec2 drawSize(frame.extent.width, frame.extent.height);
        drawSize.x = std::max(drawSize.x, 1.0f);
        drawSize.y = std::max(drawSize.y, 1.0f);
        ImGui::Image(frame.texture, drawSize);
    }
    ImGui::End();
};

void GUI::gltfWindow() {
    if(!carModel.initialized || carModel.frames.empty() || carModel.frameIndex == nullptr) return;
    gltfFrame& frame = carModel.frames[*carModel.frameIndex];
    ImGui::SetNextWindowSize(ImVec2(frame.extent.width, frame.extent.height), ImGuiCond_FirstUseEver);
    if(ImGui::Begin("gltf", NULL, 0)){
        const VkExtent2D nextExtent = quantizeContentExtent(ImGui::GetContentRegionAvail(), frame.extent);
        if (nextExtent.width != frame.extent.width || nextExtent.height != frame.extent.height) {
            frame.extent = nextExtent;
            carModel.dirty = true;
        }
        ImVec2 drawSize(frame.extent.width, frame.extent.height);
        drawSize.x = std::max(drawSize.x, 1.0f);
        drawSize.y = std::max(drawSize.y, 1.0f);
        ImGui::Image(frame.texture, drawSize);
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsItemHovered()) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                carModel.camera.yaw -= io.MouseDelta.x * carModel.camera.orbitSensitivity;
                carModel.camera.pitch += io.MouseDelta.y * carModel.camera.orbitSensitivity;
                carModel.camera.pitch = std::clamp(carModel.camera.pitch, -89.0f, 89.0f);
            }
            if (std::abs(io.MouseWheel) > 0.0f) {
                carModel.camera.distance -= io.MouseWheel * carModel.camera.zoomSensitivity;
                carModel.camera.distance = std::clamp(
                    carModel.camera.distance,
                    carModel.camera.minDistance,
                    carModel.camera.maxDistance);
            }
        }
    }
    ImGui::End();
}


void GUI::shaderWindow(){

};

void GUI::processEvents(SDL_Event* events){
    if(events == nullptr) return;
    ImGuiIO& io = ImGui::GetIO();
    switch(events->type){
        case SDL_EVENT_MOUSE_MOTION:
            io.AddMousePosEvent(events->motion.x, events->motion.y); break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const int button = sdlMouseButtonToImGuiButton(events->button.button);
            if(button >= 0){
                io.AddMousePosEvent(events->button.x, events->button.y);
                io.AddMouseButtonEvent(button, events->button.down);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            float wheelX = events->wheel.x;
            float wheelY = events->wheel.y;
            if(events->wheel.direction == SDL_MOUSEWHEEL_FLIPPED){
                wheelX = -wheelX;
                wheelY = -wheelY;
            }
            io.AddMouseWheelEvent(wheelX, wheelY);
            break;
        }
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            io.AddFocusEvent(true); break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            io.AddFocusEvent(false); break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            addModifierEvents(io, events->key.mod);
            const ImGuiKey key = sdlScancodeToImGuiKey(events->key.scancode);
            if(key != ImGuiKey_None && !(events->key.down && events->key.repeat)){
                io.AddKeyEvent(key, events->key.down);
                io.SetKeyEventNativeData(key, events->key.key, events->key.scancode, events->key.scancode);
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            if(events->text.text != nullptr) io.AddInputCharactersUTF8(events->text.text);
            break;
        default: break;
    }
}

void GUI::setStyle(){
    ImGuiStyle &style = ImGui::GetStyle();
    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.55f;
    style.TabRounding = 6.0f;
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.PopupRounding = 8.0f;
    style.GrabRounding = 999.0f;
    style.ScrollbarRounding = 999.0f;
    style.FrameRounding = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.DockingSeparatorSize = 1.0f;
    style.WindowPadding = ImVec2{18.0f, 16.0f};
    style.FramePadding = ImVec2{12.0f, 9.0f};
    style.CellPadding = ImVec2{10.0f, 8.0f};
    style.ItemSpacing = ImVec2{10.0f, 8.0f};
    style.ItemInnerSpacing = ImVec2{8.0f, 6.0f};
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    ImVec4* colors = style.Colors;
    const ImVec4 baseBg{0.09f, 0.08f, 0.07f, 1.00f};
    const ImVec4 panelBg{0.12f, 0.11f, 0.09f, 0.98f};
    const ImVec4 elevatedBg{0.13f, 0.12f, 0.11f, 1.00f};
    const ImVec4 mutedBg{0.17f, 0.17f, 0.17f, 1.00f};
    const ImVec4 border{0.18f, 0.18f, 0.19f, 1.00f};
    const ImVec4 textColor{0.83f, 0.84f, 0.86f, 1.00f};
    const ImVec4 dimText{0.57f, 0.59f, 0.62f, 1.00f};
    const ImVec4 accent{0.90f, 0.88f, 0.82f, 1.00f};
    const ImVec4 accentHover{0.96f, 0.95f, 0.91f, 1.00f};
    const ImVec4 accentActive{0.82f, 0.80f, 0.74f, 1.00f};

    colors[ImGuiCol_Text] = textColor;
    colors[ImGuiCol_TextDisabled] = dimText;
    colors[ImGuiCol_WindowBg] = baseBg;
    colors[ImGuiCol_ChildBg] = panelBg;
    colors[ImGuiCol_PopupBg] = elevatedBg;
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = ImVec4{0.00f, 0.00f, 0.00f, 0.00f};
    colors[ImGuiCol_FrameBg] = ImVec4{0.14f, 0.14f, 0.15f, 1.00f};
    colors[ImGuiCol_FrameBgHovered] = ImVec4{0.18f, 0.18f, 0.19f, 1.00f};
    colors[ImGuiCol_FrameBgActive] = ImVec4{0.20f, 0.20f, 0.21f, 1.00f};
    colors[ImGuiCol_TitleBg] = panelBg;
    colors[ImGuiCol_TitleBgActive] = elevatedBg;
    colors[ImGuiCol_TitleBgCollapsed] = panelBg;
    colors[ImGuiCol_MenuBarBg] = panelBg;
    colors[ImGuiCol_ScrollbarBg] = ImVec4{0.08f, 0.08f, 0.08f, 0.80f};
    colors[ImGuiCol_ScrollbarGrab] = ImVec4{0.23f, 0.24f, 0.25f, 1.00f};
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4{0.30f, 0.31f, 0.33f, 1.00f};
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4{0.37f, 0.38f, 0.40f, 1.00f};
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = ImVec4{0.39f, 0.40f, 0.42f, 1.00f};
    colors[ImGuiCol_SliderGrabActive] = accent;
    colors[ImGuiCol_Button] = ImVec4{0.14f, 0.14f, 0.15f, 1.00f};
    colors[ImGuiCol_ButtonHovered] = ImVec4{0.18f, 0.18f, 0.19f, 1.00f};
    colors[ImGuiCol_ButtonActive] = ImVec4{0.22f, 0.22f, 0.24f, 1.00f};
    colors[ImGuiCol_Header] = ImVec4{0.13f, 0.13f, 0.14f, 1.00f};
    colors[ImGuiCol_HeaderHovered] = ImVec4{0.17f, 0.17f, 0.18f, 1.00f};
    colors[ImGuiCol_HeaderActive] = ImVec4{0.20f, 0.20f, 0.22f, 1.00f};
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = ImVec4{0.27f, 0.28f, 0.30f, 1.00f};
    colors[ImGuiCol_SeparatorActive] = ImVec4{0.34f, 0.35f, 0.37f, 1.00f};
    colors[ImGuiCol_ResizeGrip] = ImVec4{0.24f, 0.25f, 0.27f, 0.35f};
    colors[ImGuiCol_ResizeGripHovered] = ImVec4{0.31f, 0.33f, 0.35f, 0.60f};
    colors[ImGuiCol_ResizeGripActive] = accent;
    colors[ImGuiCol_Tab] = ImVec4{0.11f, 0.11f, 0.12f, 1.00f};
    colors[ImGuiCol_TabHovered] = ImVec4{0.14f, 0.14f, 0.15f, 1.00f};
    colors[ImGuiCol_TabActive] = ImVec4{0.13f, 0.13f, 0.14f, 1.00f};
    colors[ImGuiCol_TabUnfocused] = ImVec4{0.10f, 0.10f, 0.11f, 1.00f};
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4{0.12f, 0.12f, 0.13f, 1.00f};
    colors[ImGuiCol_TabSelectedOverline] = ImVec4{0.00f, 0.00f, 0.00f, 0.00f};
    colors[ImGuiCol_DockingPreview] = ImVec4{0.90f, 0.88f, 0.82f, 0.16f};
    colors[ImGuiCol_DockingEmptyBg] = ImVec4{0.11f, 0.10f, 0.09f, 1.00f};
    colors[ImGuiCol_PlotLines] = accent;
    colors[ImGuiCol_PlotLinesHovered] = accentHover;
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_PlotHistogramHovered] = accentHover;
    colors[ImGuiCol_TableHeaderBg] = ImVec4{0.12f, 0.12f, 0.13f, 1.00f};
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = ImVec4{0.15f, 0.16f, 0.17f, 1.00f};
    colors[ImGuiCol_TableRowBg] = ImVec4{0.00f, 0.00f, 0.00f, 0.00f};
    colors[ImGuiCol_TableRowBgAlt] = ImVec4{0.12f, 0.12f, 0.13f, 0.30f};
    colors[ImGuiCol_TextSelectedBg] = ImVec4{0.90f, 0.88f, 0.82f, 0.12f};
    colors[ImGuiCol_DragDropTarget] = accentHover;
    colors[ImGuiCol_NavHighlight] = ImVec4{0.90f, 0.88f, 0.82f, 0.28f};
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4{0.90f, 0.87f, 0.82f, 0.75f};
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4{0.06f, 0.05f, 0.04f, 0.55f};
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4{0.04f, 0.03f, 0.02f, 0.72f};

    ImPlotStyle &plotStyle = ImPlot::GetStyle();
    plotStyle.Colors[ImPlotCol_FrameBg] = panelBg;
    //plotStyle.Colors[ImPlotCol_PlotBg] = ImVec4(0, 0, 0, 0.95f);
    //plotStyle.Colors[ImPlotCol_PlotBorder] = ImVec4(0, 0, 0, 0.0f);
    //plotStyle.Colors[ImPlotCol_LegendBg] = ImVec4(0, 0, 0, 0.0f);
    //plotStyle.Colors[ImPlotCol_LegendBorder] = ImVec4(0, 0, 0, 0.0f);

    /*
    ImNodesStyle &nodeStyle = ImNodes::GetStyle();
    const auto packNodeColor = [](const ImVec4 &color) {
        return ImGui::ColorConvertFloat4ToU32(color);
    };

    nodeStyle.Colors[ImNodesCol_NodeBackground] = packNodeColor(midGray);
    nodeStyle.Colors[ImNodesCol_NodeBackgroundHovered] = packNodeColor(lightGray);
    nodeStyle.Colors[ImNodesCol_NodeBackgroundSelected] = packNodeColor(lightGray);
    nodeStyle.Colors[ImNodesCol_NodeOutline] = packNodeColor(ImVec4{0.32f, 0.32f, 0.32f, 1.00f});
    nodeStyle.Colors[ImNodesCol_TitleBar] = packNodeColor(darkGray);
    nodeStyle.Colors[ImNodesCol_TitleBarHovered] = packNodeColor(midGray);
    nodeStyle.Colors[ImNodesCol_TitleBarSelected] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_Link] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_LinkHovered] = packNodeColor(ImVec4{0.20f, 0.90f, 0.90f, 1.00f});
    nodeStyle.Colors[ImNodesCol_LinkSelected] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_Pin] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_PinHovered] = packNodeColor(ImVec4{0.20f, 0.90f, 0.90f, 1.00f});
    nodeStyle.Colors[ImNodesCol_BoxSelector] = packNodeColor(ImVec4{0.00f, 0.75f, 0.75f, 0.20f});
    nodeStyle.Colors[ImNodesCol_BoxSelectorOutline] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_GridBackground] = packNodeColor(almostBlack);
    nodeStyle.Colors[ImNodesCol_GridLine] = packNodeColor(ImVec4{0.18f, 0.18f, 0.18f, 1.00f});
    nodeStyle.Colors[ImNodesCol_MiniMapBackground] = packNodeColor(darkGray);
    nodeStyle.Colors[ImNodesCol_MiniMapBackgroundHovered] = packNodeColor(midGray);
    nodeStyle.Colors[ImNodesCol_MiniMapOutline] = packNodeColor(ImVec4{0.32f, 0.32f, 0.32f, 1.00f});
    nodeStyle.Colors[ImNodesCol_MiniMapOutlineHovered] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_MiniMapNodeBackground] = packNodeColor(midGray);
    nodeStyle.Colors[ImNodesCol_MiniMapNodeBackgroundHovered] = packNodeColor(lightGray);
    nodeStyle.Colors[ImNodesCol_MiniMapNodeBackgroundSelected] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_MiniMapNodeOutline] = packNodeColor(ImVec4{0.32f, 0.32f, 0.32f, 1.00f});
    nodeStyle.Colors[ImNodesCol_MiniMapLink] = packNodeColor(lightBlue);
    nodeStyle.Colors[ImNodesCol_MiniMapLinkSelected] = packNodeColor(ImVec4{0.20f, 0.90f, 0.90f, 1.00f});
    */
};
