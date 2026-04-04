#include "gui.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include "background_frag_spv.hpp"
#include "background_vert_spv.hpp"
#include "s26track_glb.hpp"
#include "s26_simple_track_glb.hpp"
#include "newCar_glb.hpp"

void GUI::buildUI(){
    handleNetwork();
    ImGui::NewFrame();
    buildTitleBar();
    leftSideBar();
    dockspace();
    resizeHorizontalLayout();
    pages.showPage(pages.activePage);

    ImGui::Render();
};

void GUI::init(GPU* gpu, Network* network, Parse* parse){
    this->gpu = gpu;
    this->network = network;
    this->parse = parse;
    bindWindow(gpu->window);
    //sceneModel.addModel("s26track_glb", s26_simple_track_glb, s26_simple_track_glb_size, false);
    sceneModel.addModel("s26track_glb", s26track_glb, s26track_glb_size, false);
    sceneModel.addModel("newCar_glb", newCar_glb, newCar_glb_size, true);
    sceneModel.dispatchInit(*gpu);
    backgroundShader.dispatchInit(*gpu, (uint32_t*)background_vert_spv, background_vert_spv_size,
        (uint32_t*)background_frag_spv, background_frag_spv_size);
    pages.addPage("Home", [this]() { homeWindow(); });
    pages.addPage("Map", [this]() { sceneWindow(); });
    pages.addPage("Network", [this]() { networkWindow(); });
    pages.addPage("Arena", [this]() { this->parse->arena.statusUI(); });
    setStyle();
};

std::string timestampNow(){
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm localTime = *std::localtime(&time);
    std::ostringstream out;
    out << std::put_time(&localTime, "[%H:%M:%S.")
        << std::setw(3) << std::setfill('0') << ms.count() << "]";
    return out.str();
}

ImVec4 messageColor(NetworkResponseType type){
    switch(type){
        case NetworkResponseType::Error: return ImVec4(0.95f, 0.63f, 0.63f, 1.0f);
        case NetworkResponseType::Success: return ImVec4(0.68f, 0.90f, 0.72f, 1.0f);
        case NetworkResponseType::Info:
        default: return ImVec4(0.66f, 0.82f, 0.97f, 1.0f);
    }
}

const char* messagePrefix(NetworkResponseType type){
    switch(type){
        case NetworkResponseType::Error: return "[!]";
        case NetworkResponseType::Success: return "[+]";
        case NetworkResponseType::Info:
        default: return "[-]";
    }
}

void GUI::handleNetwork(){
    while(NetworkResponse* response = guiResponses.read()){
        networkStatus = response->message;
        HandlerMessage entry{};
        entry.color = messageColor(response->type);
        entry.text = timestampNow() + " " + messagePrefix(response->type) + " " + response->message;
        handlerMessages.emplace_back(std::move(entry));
        if(handlerMessages.size() > 256) handlerMessages.erase(handlerMessages.begin());
    }
};

void GUI::bindNetworkResponses(GUIResponseQueue::Reader reader){
    guiResponses = reader;
}

void GUI::queueStartProtocol(){
    NetworkCommand command{};
    command.type = NetworkCommandType::StartProtocol;
    command.config = protocolConfig;
    guiCommands.write([&](NetworkCommand& slot){ slot = command; });
}

void GUI::queueStopProtocol(){
    NetworkCommand command{};
    command.type = NetworkCommandType::StopProtocol;
    guiCommands.write([&](NetworkCommand& slot){ slot = command; });
}

void GUI::demoPlots(){
};

void GUI::homeWindow(){
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if(ImGui::Begin("home", nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse)){
        backgroundWindow();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void GUI::buildTitleBar(){
    titleBar.clearInteract();
    if (!titleBar.enabled) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = static_cast<float>(titleBar.height);
    const ImVec2 pos = viewport->Pos;
    const ImVec2 size(viewport->Size.x, height);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x, 0.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("##PhotonTitleBar", nullptr, flags)){
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Photon");

        const std::array<std::pair<const char*, WindowAction>, TitleBar::buttonCount> buttons{{
            {"-", WindowAction::Minimize},
            {"=", WindowAction::ToggleMaximize},
            {"x", WindowAction::Close},
        }};
        const float buttonWidth = height;
        const float totalButtonWidth = buttonWidth * static_cast<float>(buttons.size());
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - totalButtonWidth, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
        for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
            if (i > 0) ImGui::SameLine();
            if (ImGui::Button(buttons[i].first, ImVec2(buttonWidth, height))) {
                titleBar.pendingAction = buttons[i].second;
            }
            titleBar.addInteract(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

    if (window == nullptr) return;
    switch (titleBar.pendingAction){
        case WindowAction::Close: {
            SDL_Event quitEvent{};
            quitEvent.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quitEvent);
            break;
        }
        case WindowAction::Minimize:
            SDL_MinimizeWindow(window);
            break;
        case WindowAction::ToggleMaximize:
            if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0) SDL_RestoreWindow(window);
            else SDL_MaximizeWindow(window);
            break;
        case WindowAction::None:
            break;
    }
    titleBar.pendingAction = WindowAction::None;
}

void GUI::dockspace(){
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float splitterWidth = 3.0f;
    const float minPaneWidth = std::max(1.0f, ImGui::GetStyle().WindowMinSize.x);
    const float contentWidth = viewport->Size.x;
    const float contentHeight = std::max(1.0f, viewport->Size.y - static_cast<float>(titleBar.height));
    if (leftPaneWidth <= 0.0f) leftPaneWidth = contentWidth * 0.175f;
    leftPaneWidth = std::clamp(
        leftPaneWidth,
        minPaneWidth,
        std::max(minPaneWidth, contentWidth - splitterWidth - minPaneWidth));
    const ImVec2 dockspacePos(
        viewport->Pos.x + leftPaneWidth + splitterWidth,
        viewport->Pos.y + static_cast<float>(titleBar.height));
    const ImVec2 dockspaceSize(
        std::max(1.0f, contentWidth - leftPaneWidth - splitterWidth),
        contentHeight);
    ImGui::SetNextWindowPos(dockspacePos);
    ImGui::SetNextWindowSize(dockspaceSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    window_flags |= ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if(ImGui::Begin("Window with a DockSpace", nullptr, window_flags)){
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    pages.dockspaceID = dockspace_id;
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_AutoHideTabBar);
    }ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
}

void GUI::leftSideBar() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float splitterWidth = 3.0f;
    const float minPaneWidth = std::max(1.0f, ImGui::GetStyle().WindowMinSize.x);
    const float contentWidth = viewport->Size.x;
    const float contentHeight = std::max(1.0f, viewport->Size.y - static_cast<float>(titleBar.height));
    if (leftPaneWidth <= 0.0f) leftPaneWidth = contentWidth * 0.175f;
    leftPaneWidth = std::clamp(
        leftPaneWidth,
        minPaneWidth,
        std::max(minPaneWidth, contentWidth - splitterWidth - minPaneWidth));
    const ImVec2 sideBarPos(
        viewport->Pos.x,
        viewport->Pos.y + static_cast<float>(titleBar.height));
    const ImVec2 sideBarSize(
        leftPaneWidth,
        contentHeight);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(sideBarPos);
    ImGui::SetNextWindowSize(sideBarSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.WindowPadding);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    if (ImGui::Begin("##LeftSideBar", nullptr, windowFlags)) {
        const float itemWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        for (size_t i = 0; i < pages.entries.size(); ++i) {
            const bool selected = i == pages.activePage;
            if (ImGui::Selectable(pages.entries[i].label.c_str(), selected, 0, ImVec2(itemWidth, 0.0f))) {
                pages.activePage = i;
            }
        }
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void GUI::resizeHorizontalLayout() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float splitterWidth = 3.0f;
    const float minPaneWidth = std::max(1.0f, ImGui::GetStyle().WindowMinSize.x);
    const float contentWidth = viewport->Size.x;
    const float contentHeight = std::max(1.0f, viewport->Size.y - static_cast<float>(titleBar.height));
    if (leftPaneWidth <= 0.0f) leftPaneWidth = contentWidth * 0.175f;
    leftPaneWidth = std::clamp(
        leftPaneWidth,
        minPaneWidth,
        std::max(minPaneWidth, contentWidth - splitterWidth - minPaneWidth));
    const ImVec2 splitterPos(
        viewport->Pos.x + leftPaneWidth,
        viewport->Pos.y + static_cast<float>(titleBar.height));
    const ImVec2 splitterSize(
        splitterWidth,
        contentHeight);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(splitterPos);
    ImGui::SetNextWindowSize(splitterSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.25f, 0.27f, 0.22f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.31f, 0.33f, 0.35f, 0.38f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.40f, 0.42f, 0.45f, 0.48f));

    if (ImGui::Begin("##LeftPaneSplitter", nullptr, windowFlags)) {
        ImGui::Button("##LeftPaneSplitterHandle", splitterSize);
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (ImGui::IsItemActive()) {
            leftPaneWidth = std::clamp(
                leftPaneWidth + ImGui::GetIO().MouseDelta.x,
                minPaneWidth,
                std::max(minPaneWidth, contentWidth - splitterWidth - minPaneWidth));
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
}

VkExtent2D quantizeContentExtent(ImVec2 contentSize, VkExtent2D fallback) {
    if (contentSize.x <= 1.0f || contentSize.y <= 1.0f) return fallback;
    const uint32_t width = std::max(1u, static_cast<uint32_t>(std::lround(contentSize.x)));
    const uint32_t height = std::max(1u, static_cast<uint32_t>(std::lround(contentSize.y)));
    return {width, height};
}

void GUI::backgroundWindow(){
    const bool ready = backgroundShader.initialized.load()
        && !backgroundShader.frames.empty()
        && backgroundShader.frameIndex != nullptr;
    shaderFrame fallbackFrame{};
    shaderFrame& frame = ready ? backgroundShader.frames[*backgroundShader.frameIndex] : fallbackFrame;
    if (ready) {
        const VkExtent2D nextExtent = quantizeContentExtent(ImGui::GetContentRegionAvail(), frame.extent);
        if (nextExtent.width != frame.extent.width || nextExtent.height != frame.extent.height) {
            frame.extent = nextExtent;
            backgroundShader.dirty = true;
        }
    }
    ImVec2 drawSize(frame.extent.width, frame.extent.height);
    drawSize.x = std::max(drawSize.x, 1.0f);
    drawSize.y = std::max(drawSize.y, 1.0f);
    if (ready) ImGui::Image(frame.texture, drawSize);
    else ImGui::Text("loading shader");
};

void GUI::gltfWindow() {
    const bool ready = carModel.initialized.load()
        && !carModel.frames.empty()
        && carModel.frameIndex != nullptr;
    gltfFrame fallbackFrame{};
    gltfFrame& frame = ready ? carModel.frames[*carModel.frameIndex] : fallbackFrame;
    ImGui::SetNextWindowSize(ImVec2(frame.extent.width, frame.extent.height), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if(ImGui::Begin("gltf", NULL, 0)){
        if (ready) {
            const VkExtent2D nextExtent = quantizeContentExtent(ImGui::GetContentRegionAvail(), frame.extent);
            if (nextExtent.width != frame.extent.width || nextExtent.height != frame.extent.height) {
                frame.extent = nextExtent;
                carModel.dirty = true;
            }
        }
        ImVec2 drawSize(frame.extent.width, frame.extent.height);
        drawSize.x = std::max(drawSize.x, 1.0f);
        drawSize.y = std::max(drawSize.y, 1.0f);
        if (ready) {
            ImGui::Image(frame.texture, drawSize);
            ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsItemHovered()) {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    carModel.camera.yaw -= io.MouseDelta.x * carModel.camera.orbitSensitivity;
                    carModel.camera.pitch += io.MouseDelta.y * carModel.camera.orbitSensitivity;
                    carModel.camera.pitch = std::clamp(carModel.camera.pitch, -89.0f, 89.0f);
                }
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                    const float yawRadians = glm::radians(carModel.camera.yaw);
                    const float pitchRadians = glm::radians(carModel.camera.pitch);
                    const glm::vec3 front = glm::normalize(glm::vec3(
                        -std::cos(pitchRadians) * std::cos(yawRadians),
                        -std::cos(pitchRadians) * std::sin(yawRadians),
                        -std::sin(pitchRadians)));
                    const glm::vec3 right = glm::normalize(glm::cross(front, carModel.camera.up));
                    const glm::vec3 cameraUp = glm::normalize(glm::cross(right, front));
                    const float viewportHeight = std::max(drawSize.y, 1.0f);
                    const float worldUnitsPerPixel =
                        (2.0f * carModel.camera.distance * std::tan(glm::radians(93.0f) * 0.5f)) / viewportHeight;
                    const glm::vec3 panOffset =
                        (-right * io.MouseDelta.x + cameraUp * io.MouseDelta.y)
                        * worldUnitsPerPixel * carModel.camera.panSensitivity;
                    carModel.camera.target += panOffset;
                }
                if (std::abs(io.MouseWheel) > 0.0f) {
                    const float zoomScale = std::max(0.1f, 1.0f - io.MouseWheel * carModel.camera.zoomSensitivity);
                    carModel.camera.distance *= zoomScale;
                    carModel.camera.distance = std::clamp(
                        carModel.camera.distance,
                        carModel.camera.minDistance,
                        carModel.camera.maxDistance);
                }
            }
        } else { ImGui::Text("loading model"); }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

void GUI::sceneWindow(){
    if (sceneModel.trackedObjectIndex >= 0
        && sceneModel.trackedObjectIndex < static_cast<int>(sceneModel.objects.size())) {
        sceneModel.objects[sceneModel.trackedObjectIndex].position.z = 1.0f;
    }

    const bool ready = sceneModel.initialized.load()
        && !sceneModel.frames.empty()
        && sceneModel.frameIndex != nullptr;
    SceneFrame fallbackFrame{};
    SceneFrame& frame = ready ? sceneModel.frames[*sceneModel.frameIndex] : fallbackFrame;

    ImGui::SetNextWindowSize(ImVec2(frame.extent.width, frame.extent.height), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    if (ImGui::Begin("scene", nullptr, ImGuiWindowFlags_NoTitleBar)) {
        if (ready) {
            const VkExtent2D nextExtent = quantizeContentExtent(ImGui::GetContentRegionAvail(), frame.extent);
            if (nextExtent.width != frame.extent.width || nextExtent.height != frame.extent.height) {
                frame.extent = nextExtent;
                sceneModel.dirty = true;
            }
        }

        ImVec2 drawSize(frame.extent.width, frame.extent.height);
        drawSize.x = std::max(drawSize.x, 1.0f);
        drawSize.y = std::max(drawSize.y, 1.0f);
        if (ready) {
            ImGui::Image(frame.texture, drawSize);
            const bool sceneHovered = ImGui::IsItemHovered();
            const ImVec2 imageMin = ImGui::GetItemRectMin();
            const ImVec2 imageMax = ImGui::GetItemRectMax();

            const char* cameraModes[] = {"Free", "Track"};
            int cameraMode = sceneModel.cameraMode == SceneCameraMode::TrackModel ? 1 : 0;
            const float overlayWidth = 120.0f;
            const ImVec2 comboPos(imageMin.x, imageMax.y - 38.0f);
            ImGui::SetCursorScreenPos(comboPos);
            ImGui::PushItemWidth(overlayWidth);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.11f, 0.88f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.14f, 0.14f, 0.15f, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.16f, 0.16f, 0.17f, 0.94f));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.08f, 0.09f, 0.96f));
            if (ImGui::Combo("##SceneCameraMode", &cameraMode, cameraModes, IM_ARRAYSIZE(cameraModes))) {
                sceneModel.cameraMode = cameraMode == 1 ? SceneCameraMode::TrackModel : SceneCameraMode::Free;
            }
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();
            ImGui::PopItemWidth();

            ImGuiIO& io = ImGui::GetIO();
            if (sceneHovered) {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    sceneModel.camera.yaw -= io.MouseDelta.x * sceneModel.camera.orbitSensitivity;
                    sceneModel.camera.pitch += io.MouseDelta.y * sceneModel.camera.orbitSensitivity;
                    sceneModel.camera.pitch = std::clamp(sceneModel.camera.pitch, -89.0f, 89.0f);
                }
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                    if (sceneModel.cameraMode == SceneCameraMode::Free) {
                        const float yawRadians = glm::radians(sceneModel.camera.yaw);
                        const float pitchRadians = glm::radians(sceneModel.camera.pitch);
                        const glm::vec3 front = glm::normalize(glm::vec3(
                            -std::cos(pitchRadians) * std::cos(yawRadians),
                            -std::cos(pitchRadians) * std::sin(yawRadians),
                            -std::sin(pitchRadians)));
                        const glm::vec3 right = glm::normalize(glm::cross(front, sceneModel.camera.up));
                        const glm::vec3 cameraUp = glm::normalize(glm::cross(right, front));
                        const float viewportHeight = std::max(drawSize.y, 1.0f);
                        const float worldUnitsPerPixel =
                            (2.0f * sceneModel.camera.distance * std::tan(glm::radians(93.0f) * 0.5f)) / viewportHeight;
                        const glm::vec3 panOffset =
                            (-right * io.MouseDelta.x + cameraUp * io.MouseDelta.y)
                            * worldUnitsPerPixel * sceneModel.camera.panSensitivity;
                        sceneModel.camera.target += panOffset;
                    }
                }
                if (std::abs(io.MouseWheel) > 0.0f) {
                    const float zoomScale = std::max(0.1f, 1.0f - io.MouseWheel * sceneModel.camera.zoomSensitivity);
                    sceneModel.camera.distance *= zoomScale;
                    sceneModel.camera.distance = std::clamp(
                        sceneModel.camera.distance,
                        sceneModel.camera.minDistance,
                        sceneModel.camera.maxDistance);
                }
            }
        } else {
            ImGui::Text("loading scene");
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
};

void GUI::networkWindow(){
    if(ImGui::Begin("network", nullptr, ImGuiWindowFlags_NoTitleBar)){
        const float totalHeight = ImGui::GetContentRegionAvail().y;
        int protocol = static_cast<int>((protocolConfig.kind == ProtocolKind::None ? ProtocolKind::TCP : protocolConfig.kind)) - 1;
        const char* protocolNames[] = {"TCP", "UDP", "UART", "SocketCAN"};
        ImGui::SetNextItemWidth(220.0f);
        if(ImGui::Combo("Protocol", &protocol, protocolNames, IM_ARRAYSIZE(protocolNames))){
            protocolConfig.kind = static_cast<ProtocolKind>(protocol + 1);
        }

        if(protocolConfig.kind == ProtocolKind::None) protocolConfig.kind = ProtocolKind::TCP;
        ImGui::Text("selected protocol: %s", Protocols::name(protocolConfig.kind));

        switch(protocolConfig.kind){
            case ProtocolKind::TCP:
                ImGui::InputText("TCP IP", protocolConfig.tcp.ip, IM_ARRAYSIZE(protocolConfig.tcp.ip));
                ImGui::InputScalar("TCP Port", ImGuiDataType_U16, &protocolConfig.tcp.port);
                break;
            case ProtocolKind::UDP:
                ImGui::InputText("UDP IP", protocolConfig.udp.ip, IM_ARRAYSIZE(protocolConfig.udp.ip));
                ImGui::InputScalar("UDP Port", ImGuiDataType_U16, &protocolConfig.udp.port);
                ImGui::InputText("Subscribe Message",
                    protocolConfig.udp.subscribeMessage,
                    IM_ARRAYSIZE(protocolConfig.udp.subscribeMessage));
                break;
            case ProtocolKind::UART:
                ImGui::InputText("UART Device", protocolConfig.uart.device, IM_ARRAYSIZE(protocolConfig.uart.device));
                ImGui::InputScalar("UART Baud", ImGuiDataType_U32, &protocolConfig.uart.baudRate);
                break;
            case ProtocolKind::SocketCAN:
#ifdef _WIN32
                ImGui::InputText("PCAN Channel",
                    protocolConfig.socketCAN.channel,
                    IM_ARRAYSIZE(protocolConfig.socketCAN.channel));
                ImGui::InputText("PCAN Bitrate",
                    protocolConfig.socketCAN.bitrate,
                    IM_ARRAYSIZE(protocolConfig.socketCAN.bitrate));
                ImGui::Checkbox("Listen Only", &protocolConfig.socketCAN.listenOnly);
                ImGui::Checkbox("Bus-Off Reset", &protocolConfig.socketCAN.busoffReset);
#else
                ImGui::InputText("CAN Interface",
                    protocolConfig.socketCAN.interfaceName,
                    IM_ARRAYSIZE(protocolConfig.socketCAN.interfaceName));
                ImGui::InputScalar("CAN Bitrate", ImGuiDataType_U32, &protocolConfig.socketCAN.dataRate);
#endif
                break;
            case ProtocolKind::None:
            default:
                break;
        }

        if(ImGui::Button("Start Protocol")) queueStartProtocol();
        if(ImGui::Button("Stop Protocol")) queueStopProtocol();
        ImGui::SameLine();
        if(ImGui::Button("Clear Messages")) handlerMessages.clear();

        const float footerHeight = totalHeight * 0.28f;
        const float spacerHeight = std::max(0.0f, ImGui::GetContentRegionAvail().y - (totalHeight * 0.30f));
        ImGui::Dummy(ImVec2(0.0f, spacerHeight));
        if(ImGui::BeginChild("##network_messages", ImVec2(0.0f, footerHeight), true)){
            for(const HandlerMessage& message : handlerMessages){
                ImGui::PushStyleColor(ImGuiCol_Text, message.color);
                ImGui::TextUnformatted(message.text.c_str());
                ImGui::PopStyleColor();
            }
            if(ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
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
            const int button = sdlMouseToImgui(events->button.button);
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
            addModifier(io, events->key.mod);
            const ImGuiKey key = sdlToImgui(events->key.scancode);
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

void GUI::bindWindow(SDL_Window* targetWindow){ window = targetWindow; }

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
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.DockingSeparatorSize = 1.0f;
    style.WindowPadding = ImVec2{18.0f, 16.0f};
    style.FramePadding = ImVec2{12.0f, 9.0f};
    style.CellPadding = ImVec2{14.0f, 8.0f};
    style.ItemSpacing = ImVec2{10.0f, 8.0f};
    style.ItemInnerSpacing = ImVec2{8.0f, 6.0f};
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    ImVec4* colors = style.Colors;
    const ImVec4 baseBg{0.09f, 0.08f, 0.07f, 1.00f};
    const ImVec4 panelBg{0.12f, 0.11f, 0.09f, 0.98f};
    const ImVec4 elevatedBg{0.13f, 0.12f, 0.11f, 1.00f};
    const ImVec4 border{0.18f, 0.18f, 0.19f, 1.00f};
    const ImVec4 textColor{0.83f, 0.84f, 0.86f, 1.00f};
    const ImVec4 dimText{0.57f, 0.59f, 0.62f, 1.00f};
    const ImVec4 accent{0.90f, 0.88f, 0.82f, 1.00f};
    const ImVec4 accentHover{0.96f, 0.95f, 0.91f, 1.00f};

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
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4{1.00f, 1.00f, 1.00f, 1.00f};
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

ImGuiKey GUI::sdlToImgui(SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_TAB: return ImGuiKey_Tab;
        case SDL_SCANCODE_LEFT: return ImGuiKey_LeftArrow;
        case SDL_SCANCODE_RIGHT: return ImGuiKey_RightArrow;
        case SDL_SCANCODE_UP: return ImGuiKey_UpArrow;
        case SDL_SCANCODE_DOWN: return ImGuiKey_DownArrow;
        case SDL_SCANCODE_PAGEUP: return ImGuiKey_PageUp;
        case SDL_SCANCODE_PAGEDOWN: return ImGuiKey_PageDown;
        case SDL_SCANCODE_HOME: return ImGuiKey_Home;
        case SDL_SCANCODE_END: return ImGuiKey_End;
        case SDL_SCANCODE_INSERT: return ImGuiKey_Insert;
        case SDL_SCANCODE_DELETE: return ImGuiKey_Delete;
        case SDL_SCANCODE_BACKSPACE: return ImGuiKey_Backspace;
        case SDL_SCANCODE_SPACE: return ImGuiKey_Space;
        case SDL_SCANCODE_RETURN: return ImGuiKey_Enter;
        case SDL_SCANCODE_ESCAPE: return ImGuiKey_Escape;
        case SDL_SCANCODE_APOSTROPHE: return ImGuiKey_Apostrophe;
        case SDL_SCANCODE_COMMA: return ImGuiKey_Comma;
        case SDL_SCANCODE_MINUS: return ImGuiKey_Minus;
        case SDL_SCANCODE_PERIOD: return ImGuiKey_Period;
        case SDL_SCANCODE_SLASH: return ImGuiKey_Slash;
        case SDL_SCANCODE_SEMICOLON: return ImGuiKey_Semicolon;
        case SDL_SCANCODE_EQUALS: return ImGuiKey_Equal;
        case SDL_SCANCODE_LEFTBRACKET: return ImGuiKey_LeftBracket;
        case SDL_SCANCODE_BACKSLASH: return ImGuiKey_Backslash;
        case SDL_SCANCODE_RIGHTBRACKET: return ImGuiKey_RightBracket;
        case SDL_SCANCODE_GRAVE: return ImGuiKey_GraveAccent;
        case SDL_SCANCODE_CAPSLOCK: return ImGuiKey_CapsLock;
        case SDL_SCANCODE_SCROLLLOCK: return ImGuiKey_ScrollLock;
        case SDL_SCANCODE_NUMLOCKCLEAR: return ImGuiKey_NumLock;
        case SDL_SCANCODE_PRINTSCREEN: return ImGuiKey_PrintScreen;
        case SDL_SCANCODE_PAUSE: return ImGuiKey_Pause;
        case SDL_SCANCODE_KP_0: return ImGuiKey_Keypad0;
        case SDL_SCANCODE_KP_1: return ImGuiKey_Keypad1;
        case SDL_SCANCODE_KP_2: return ImGuiKey_Keypad2;
        case SDL_SCANCODE_KP_3: return ImGuiKey_Keypad3;
        case SDL_SCANCODE_KP_4: return ImGuiKey_Keypad4;
        case SDL_SCANCODE_KP_5: return ImGuiKey_Keypad5;
        case SDL_SCANCODE_KP_6: return ImGuiKey_Keypad6;
        case SDL_SCANCODE_KP_7: return ImGuiKey_Keypad7;
        case SDL_SCANCODE_KP_8: return ImGuiKey_Keypad8;
        case SDL_SCANCODE_KP_9: return ImGuiKey_Keypad9;
        case SDL_SCANCODE_KP_PERIOD: return ImGuiKey_KeypadDecimal;
        case SDL_SCANCODE_KP_DIVIDE: return ImGuiKey_KeypadDivide;
        case SDL_SCANCODE_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
        case SDL_SCANCODE_KP_MINUS: return ImGuiKey_KeypadSubtract;
        case SDL_SCANCODE_KP_PLUS: return ImGuiKey_KeypadAdd;
        case SDL_SCANCODE_KP_ENTER: return ImGuiKey_KeypadEnter;
        case SDL_SCANCODE_KP_EQUALS: return ImGuiKey_KeypadEqual;
        case SDL_SCANCODE_LCTRL: return ImGuiKey_LeftCtrl;
        case SDL_SCANCODE_LSHIFT: return ImGuiKey_LeftShift;
        case SDL_SCANCODE_LALT: return ImGuiKey_LeftAlt;
        case SDL_SCANCODE_LGUI: return ImGuiKey_LeftSuper;
        case SDL_SCANCODE_RCTRL: return ImGuiKey_RightCtrl;
        case SDL_SCANCODE_RSHIFT: return ImGuiKey_RightShift;
        case SDL_SCANCODE_RALT: return ImGuiKey_RightAlt;
        case SDL_SCANCODE_RGUI: return ImGuiKey_RightSuper;
        case SDL_SCANCODE_MENU: return ImGuiKey_Menu;
        case SDL_SCANCODE_0: return ImGuiKey_0;
        case SDL_SCANCODE_1: return ImGuiKey_1;
        case SDL_SCANCODE_2: return ImGuiKey_2;
        case SDL_SCANCODE_3: return ImGuiKey_3;
        case SDL_SCANCODE_4: return ImGuiKey_4;
        case SDL_SCANCODE_5: return ImGuiKey_5;
        case SDL_SCANCODE_6: return ImGuiKey_6;
        case SDL_SCANCODE_7: return ImGuiKey_7;
        case SDL_SCANCODE_8: return ImGuiKey_8;
        case SDL_SCANCODE_9: return ImGuiKey_9;
        case SDL_SCANCODE_A: return ImGuiKey_A;
        case SDL_SCANCODE_B: return ImGuiKey_B;
        case SDL_SCANCODE_C: return ImGuiKey_C;
        case SDL_SCANCODE_D: return ImGuiKey_D;
        case SDL_SCANCODE_E: return ImGuiKey_E;
        case SDL_SCANCODE_F: return ImGuiKey_F;
        case SDL_SCANCODE_G: return ImGuiKey_G;
        case SDL_SCANCODE_H: return ImGuiKey_H;
        case SDL_SCANCODE_I: return ImGuiKey_I;
        case SDL_SCANCODE_J: return ImGuiKey_J;
        case SDL_SCANCODE_K: return ImGuiKey_K;
        case SDL_SCANCODE_L: return ImGuiKey_L;
        case SDL_SCANCODE_M: return ImGuiKey_M;
        case SDL_SCANCODE_N: return ImGuiKey_N;
        case SDL_SCANCODE_O: return ImGuiKey_O;
        case SDL_SCANCODE_P: return ImGuiKey_P;
        case SDL_SCANCODE_Q: return ImGuiKey_Q;
        case SDL_SCANCODE_R: return ImGuiKey_R;
        case SDL_SCANCODE_S: return ImGuiKey_S;
        case SDL_SCANCODE_T: return ImGuiKey_T;
        case SDL_SCANCODE_U: return ImGuiKey_U;
        case SDL_SCANCODE_V: return ImGuiKey_V;
        case SDL_SCANCODE_W: return ImGuiKey_W;
        case SDL_SCANCODE_X: return ImGuiKey_X;
        case SDL_SCANCODE_Y: return ImGuiKey_Y;
        case SDL_SCANCODE_Z: return ImGuiKey_Z;
        case SDL_SCANCODE_F1: return ImGuiKey_F1;
        case SDL_SCANCODE_F2: return ImGuiKey_F2;
        case SDL_SCANCODE_F3: return ImGuiKey_F3;
        case SDL_SCANCODE_F4: return ImGuiKey_F4;
        case SDL_SCANCODE_F5: return ImGuiKey_F5;
        case SDL_SCANCODE_F6: return ImGuiKey_F6;
        case SDL_SCANCODE_F7: return ImGuiKey_F7;
        case SDL_SCANCODE_F8: return ImGuiKey_F8;
        case SDL_SCANCODE_F9: return ImGuiKey_F9;
        case SDL_SCANCODE_F10: return ImGuiKey_F10;
        case SDL_SCANCODE_F11: return ImGuiKey_F11;
        case SDL_SCANCODE_F12: return ImGuiKey_F12;
        default: return ImGuiKey_None;
    }
}

inline void GUI::addModifier(ImGuiIO& io, SDL_Keymod mod) {
    io.AddKeyEvent(ImGuiMod_Ctrl, (mod & SDL_KMOD_CTRL) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (mod & SDL_KMOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (mod & SDL_KMOD_ALT) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (mod & SDL_KMOD_GUI) != 0);
}

inline int GUI::sdlMouseToImgui(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return 0;
        case SDL_BUTTON_RIGHT: return 1;
        case SDL_BUTTON_MIDDLE: return 2;
        case SDL_BUTTON_X1: return 3;
        case SDL_BUTTON_X2: return 4;
        default: return -1;
    }
}

void TitleBar::clearInteract(){
    interactiveRectCount = 0;
    for (SDL_Rect& rect : interactiveRects)
        rect = SDL_Rect{0, 0, 0, 0};
}

void TitleBar::addInteract(const ImVec2& min, const ImVec2& max){
    if (interactiveRectCount >= buttonCount) return;
    SDL_Rect& rect = interactiveRects[interactiveRectCount++];
    rect.x = static_cast<int>(min.x);
    rect.y = static_cast<int>(min.y);
    rect.w = static_cast<int>(max.x - min.x);
    rect.h = static_cast<int>(max.y - min.y);
}

bool TitleBar::isInteract(int x, int y) const {
    for (int i = 0; i < interactiveRectCount; ++i) {
        const SDL_Rect& rect = interactiveRects[i];
        if ((x >= rect.x) && (x < rect.x + rect.w) &&
            (y >= rect.y) && (y < rect.y + rect.h))
            return true;
    }
    return false;
}
