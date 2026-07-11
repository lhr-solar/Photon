#include "gui.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "../gpu/shader.hpp"
#include "DDash/dashboard_tab.h"
#include "arena.hpp"
#include "config.hpp"
#include "customView.hpp"
#include "custom_shader_vert_spv.hpp"
#include "glowButton_frag_spv.hpp"
#include "gpuGui.hpp"
#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imnodes.h"
#include "implot.h"
#include "lens_frag_spv.hpp"
#include "newCar_glb.hpp"
#include "plots.hpp"
#include "track_glb.hpp"
#include "uiComponents.hpp"
#include "widget.hpp"

void GUI::init(GPU& gpu, Arena& arena, Network& network) {
  this->gpu = &gpu;
  this->arena = &arena;
  this->network = &network;
  plotManager().init(&arena);
  customViewTab().init(&arena, gpu.window);
  GuiSettings::regster(&settings);
  settings.setStyle();
  setTabs();
  updater.queryReleaseInfoOnceAsync();
  testShader.dispatchInit(gpu, (uint32_t*)custom_shader_vert_spv, custom_shader_vert_spv_size,
                          (uint32_t*)lens_frag_spv, lens_frag_spv_size);
  buttonShader.dispatchInit(gpu, (uint32_t*)custom_shader_vert_spv, custom_shader_vert_spv_size,
                            (uint32_t*)glowButton_frag_spv, glowButton_frag_spv_size);

  scene.addModel("track_glb", track_glb, track_glb_size, false);
  scene.addModel("newCar_glb", newCar_glb, newCar_glb_size, true);
  scene.dispatchInit(gpu);
}

void GUI::render() {
  if (!testShader.initialized.load() && testShader.partInitialized.load())
    testShader.finishInit(*gpu);
  if (testShader.showing) {
    testShader.render(*gpu, gpu->commandBuffers[gpu->frameIndex]);
    testShader.showing = false;
  }

  if (!buttonShader.initialized.load() && buttonShader.partInitialized.load())
    buttonShader.finishInit(*gpu);
  if (buttonShader.showing) {
    buttonShader.render(*gpu, gpu->commandBuffers[gpu->frameIndex]);
    buttonShader.showing = false;
  }

  if (!scene.initialized.load() && scene.partInitialized.load()) scene.finishInit(*gpu);
  if (scene.showing) {
    scene.render(*gpu, gpu->commandBuffers[gpu->frameIndex]);
    scene.showing = false;
  }
};

void GUI::destroy() {
  if (sideBar.backgroundTexture) {
    ImGui::UnregisterUserTexture(sideBar.backgroundTexture);
    sideBar.backgroundTexture->SetStatus(ImTextureStatus_WantDestroy);
    sideBar.backgroundTexture = nullptr;
  }
  testShader.destroy();
  buttonShader.destroy();
  scene.destroy();
};

void GUI::setFont() {
  bool incFlag = false;
  bool decFlag = false;
  auto incSize = [&]() -> void {
    settings.fontSize += 1.0f;
    ImGui::GetStyle().FontSizeBase = settings.fontSize;
    ImGui::MarkIniSettingsDirty();
  };
  auto decSize = [&]() -> void {
    settings.fontSize = settings.fontSize > 1.0f ? settings.fontSize - 1.0f : 1.0f;
    ImGui::GetStyle().FontSizeBase = settings.fontSize;
    ImGui::MarkIniSettingsDirty();
  };
  ifKey(ImGuiKey_Equal, incFlag, incSize);
  ifKey(ImGuiKey_Minus, decFlag, decSize);
};

void GUI::settingsUI() {
  const bool open = PhotonUi::beginModal("Settings", {440.0f, 230.0f});
  if (open) {
    const PhotonUi::Palette palette = PhotonUi::palette();
    PhotonUi::label("Settings", palette);
    if (PhotonUi::modalCloseButton("CloseSettings", palette)) ImGui::CloseCurrentPopup();
  }
  PhotonUi::endModal(open);
};

void GUI::updateUI() { updater.drawUI(updateAvailable); };

void GUI::exportUI() {
  const bool open = PhotonUi::beginModal("Export", {420.0f, 220.0f});
  if (open) {
    const PhotonUi::Palette palette = PhotonUi::palette();
    PhotonUi::label("Export", palette);
    if (PhotonUi::modalCloseButton("CloseExport", palette)) ImGui::CloseCurrentPopup();
  }
  PhotonUi::endModal(open);
};

void GUI::plotTest(ImGuiWindowFlags flags) {
  if (ImGui::Begin("Plots", nullptr, flags)) {
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.y = 0.0f;
    const ImPlotSpec spec = settings.plotLineSpec;
    for (const uint32_t id : arena->validIds) {
      if (id >= arena->messages.size() || !arena->messages[id]) continue;
      for (uint32_t signal = 0; signal < arena->messages[id]->signalCount; ++signal)
        Plots::signal(*arena, id, signal, size, spec);
    }
  }
  ImGui::End();
};

void GUI::carMap(ImGuiWindowFlags flags) {
  scene.showing = false;
  const bool ready =
      scene.initialized.load() && !scene.frames.empty() && scene.frameIndex != nullptr;
  SceneFrame fallbackFrame{};
  SceneFrame& frame = ready ? scene.frames[*scene.frameIndex] : fallbackFrame;

  ImGui::SetNextWindowBgAlpha(0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

  if (ImGui::Begin("scene", nullptr, flags)) {
    ImVec2 drawSize = ImGui::GetContentRegionAvail();
    drawSize.x = std::max(drawSize.x, 1.0f);
    drawSize.y = std::max(drawSize.y, 1.0f);
    if (ready) {
      const VkExtent2D nextExtent = quantizeContentExtent(drawSize, frame.extent);
      if (nextExtent.width != frame.extent.width || nextExtent.height != frame.extent.height) {
        frame.extent = nextExtent;
        scene.dirty = true;
      }
    }

    if (ready) {
      const bool sceneVisible = ImGui::IsRectVisible(drawSize);
      if (sceneVisible) scene.showing = true;
      ImGui::Image(frame.texture, drawSize);
      const bool sceneHovered = ImGui::IsItemHovered();
      const ImVec2 imageMin = ImGui::GetItemRectMin();
      const ImVec2 imageMax = ImGui::GetItemRectMax();
      const bool hasTrackedObject =
          scene.trackedObjectIndex >= 0 &&
          scene.trackedObjectIndex < static_cast<int>(scene.objects.size());
      const Position trackedPosition =
          hasTrackedObject ? scene.objects[scene.trackedObjectIndex].position : Position{};

      const char* cameraModes[] = {"Free", "Track"};
      int cameraMode = scene.cameraMode == SceneCameraMode::TrackModel ? 1 : 0;
      const float overlayWidth = 120.0f;
      const ImVec2 comboPos(imageMin.x + 12.0f, imageMin.y + 12.0f);
      ImGui::SetCursorScreenPos(comboPos);
      ImGui::PushItemWidth(overlayWidth);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.11f, 0.88f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.14f, 0.14f, 0.15f, 0.92f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.16f, 0.16f, 0.17f, 0.94f));
      ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.08f, 0.09f, 0.96f));
      if (ImGui::Combo("##SceneCameraMode", &cameraMode, cameraModes, IM_ARRAYSIZE(cameraModes))) {
        scene.cameraMode = cameraMode == 1 ? SceneCameraMode::TrackModel : SceneCameraMode::Free;
      }
      ImGui::PopStyleColor(4);
      ImGui::PopStyleVar();
      ImGui::PopItemWidth();

      char positionLabel[128]{};
      std::snprintf(positionLabel, sizeof(positionLabel), "x: %.3f | y: %.3f | z: %.3f",
                    trackedPosition.x, trackedPosition.y, trackedPosition.z);
      const ImVec2 textPadding(10.0f, 6.0f);
      const ImVec2 textSize = ImGui::CalcTextSize(positionLabel);
      const ImVec2 textMin(imageMin.x + 12.0f,
                           imageMax.y - textSize.y - textPadding.y * 2.0f - 12.0f);
      const ImVec2 textMax(textMin.x + textSize.x + textPadding.x * 2.0f,
                           textMin.y + textSize.y + textPadding.y * 2.0f);
      ImDrawList* drawList = ImGui::GetWindowDrawList();
      drawList->AddRectFilled(textMin, textMax,
                              ImGui::GetColorU32(ImVec4(0.10f, 0.10f, 0.11f, 0.88f)), 6.0f);
      drawList->AddText(ImVec2(textMin.x + textPadding.x, textMin.y + textPadding.y),
                        ImGui::GetColorU32(ImGuiCol_Text), positionLabel);

      ImGuiIO& io = ImGui::GetIO();
      if (sceneHovered) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
          scene.camera.yaw -= io.MouseDelta.x * scene.camera.orbitSensitivity;
          scene.camera.pitch += io.MouseDelta.y * scene.camera.orbitSensitivity;
          scene.camera.pitch = std::clamp(scene.camera.pitch, -89.0f, 89.0f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
          if (scene.cameraMode == SceneCameraMode::Free) {
            const float yawRadians = glm::radians(scene.camera.yaw);
            const float pitchRadians = glm::radians(scene.camera.pitch);
            const glm::vec3 front = glm::normalize(
                glm::vec3(-std::cos(pitchRadians) * std::cos(yawRadians),
                          -std::cos(pitchRadians) * std::sin(yawRadians), -std::sin(pitchRadians)));
            const glm::vec3 right = glm::normalize(glm::cross(front, scene.camera.up));
            const glm::vec3 cameraUp = glm::normalize(glm::cross(right, front));
            const float viewportHeight = std::max(drawSize.y, 1.0f);
            const float worldUnitsPerPixel =
                (2.0f * scene.camera.distance * std::tan(glm::radians(93.0f) * 0.5f)) /
                viewportHeight;
            const glm::vec3 panOffset = (-right * io.MouseDelta.x + cameraUp * io.MouseDelta.y) *
                                        worldUnitsPerPixel * scene.camera.panSensitivity;
            scene.camera.target += panOffset;
          }
        }
        if (std::abs(io.MouseWheel) > 0.0f) {
          const float zoomScale =
              std::max(0.1f, 1.0f - io.MouseWheel * scene.camera.zoomSensitivity);
          scene.camera.distance *= zoomScale;
          scene.camera.distance =
              std::clamp(scene.camera.distance, scene.camera.minDistance, scene.camera.maxDistance);
        }
      }
    } else {
      ImGui::Text("loading scene");
    }
  }
  ImGui::End();
  ImGui::PopStyleColor(2);
};

void GUI::shaderTest(ImGuiWindowFlags flags) {
  testShader.showing = false;
  flags |= ImGuiWindowFlags_NoBackground;
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  if (ImGui::Begin("shader test", nullptr, flags)) {
    const bool ready = testShader.initialized.load() && !testShader.frames.empty() &&
                       testShader.frameIndex != nullptr;
    shaderFrame fallbackFrame{};
    shaderFrame& frame = ready ? testShader.frames[*testShader.frameIndex] : fallbackFrame;
    if (ready) {
      const VkExtent2D nextExtent =
          quantizeContentExtent(ImGui::GetContentRegionAvail(), frame.extent);
      if (nextExtent.width != frame.extent.width || nextExtent.height != frame.extent.height) {
        frame.extent = nextExtent;
        testShader.dirty = true;
      }
      ImVec2 drawSize(frame.extent.width, frame.extent.height);
      drawSize.x = std::max(drawSize.x, 1.0f);
      drawSize.y = std::max(drawSize.y, 1.0f);
      if (ImGui::IsRectVisible(drawSize)) {
        testShader.showing = true;
        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
        const ImVec2 imageMax(imageMin.x + drawSize.x, imageMin.y + drawSize.y);
        ImGui::GetWindowDrawList()->AddImage(frame.texture, imageMin, imageMax);
        ImGui::Dummy(drawSize);
      } else {
        ImGui::Dummy(drawSize);
      };
    } else
      ImGui::Text("loading shader");
  }
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
};

void GUI::drawButtonShaderOverlay(ImVec2 buttonMin, ImVec2 buttonMax) {
  if (!updateAvailable) return;
  const bool ready = buttonShader.initialized.load() && !buttonShader.frames.empty() &&
                     buttonShader.frameIndex != nullptr;
  if (!ready) return;

  shaderFrame& frame = buttonShader.frames[*buttonShader.frameIndex];
  constexpr float expandX = 28.0f;
  constexpr float expandY = 22.0f;
  const ImVec2 overlayMin(buttonMin.x - expandX, buttonMin.y - expandY);
  const ImVec2 overlayMax(buttonMax.x + expandX, buttonMax.y + expandY);
  const ImVec2 overlaySize(overlayMax.x - overlayMin.x, overlayMax.y - overlayMin.y);

  const VkExtent2D nextExtent = quantizeContentExtent(overlaySize, frame.extent);
  if (nextExtent.width != frame.extent.width || nextExtent.height != frame.extent.height) {
    frame.extent = nextExtent;
    buttonShader.dirty = true;
  }

  if (ImGui::IsRectVisible(overlayMin, overlayMax)) {
    buttonShader.showing = true;
    ImGui::GetWindowDrawList()->AddImage(frame.texture, overlayMin, overlayMax);
  }
}

void GUI::testFunc(ImGuiWindowFlags flags) {
  if (ImGui::Begin("test page", NULL, flags)) {
    ImGui::Text("wasldfkjasdlfkj");
    bool val1 = ImGui::Button("button1");
    bool val2 = ImGui::Button("button2");
    ImGui::PushID(0);
    Widget::animTextBox("some text here", val1);
    ImGui::PopID();
    ImGui::PushID(1);
    Widget::animTextBox("new text!", false);
    ImGui::PopID();
    ImGui::NewLine();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(2);
    Widget::animLine(p, {p.x + 240, p.y}, val1);
    ImGui::PopID();
    ImGui::Text("something random");
    p = ImGui::GetCursorScreenPos();
    ImGui::PushID(3);
    Widget::animLine(p, {p.x + 240, p.y}, val2);
    ImGui::PopID();
    ImGui::PushID(4);
    Widget::animLine(p, {p.x + 240, p.y + 240}, val2);
    ImGui::PopID();
    ImGui::PushID(5);
    Widget::animLine(p, {p.x, p.y + 240}, val2);
    ImGui::PopID();
    ImGui::Text("let us see...");
    auto draw = ImGui::GetWindowDrawList();

    float s = 50;
    p = ImGui::GetCursorScreenPos();
    draw->AddRectFilledMultiColor(p, {p.x + s, p.y + s}, IM_COL32(255, 0, 0, 255),
                                  IM_COL32(0, 255, 0, 255), IM_COL32(0, 0, 255, 255),
                                  IM_COL32(255, 255, 255, 255));
    ImGui::NewLine();
    ImGui::NewLine();
  }
  ImGui::End();
};

void GUI::setTabs() {
  // Hot-reloaded UI modules own separate function-local singletons, so bind them lazily.
  plotManager().init(arena);
  customViewTab().init(arena, gpu ? gpu->window : nullptr);
  tabs.list.clear();
  tabs.list.push_back(Tab::bind<GUI, &GUI::plotTest>(*this, "Plots"));
  tabs.list.push_back(
      Tab::bind<CustomViewTab, &CustomViewTab::draw>(customViewTab(), "Custom Views"));
  tabs.list.push_back(Tab::bind<Arena, &Arena::statusUI>(*arena, "Arena"));
  tabs.list.push_back(Tab::bind<GUI, &GUI::networkPage>(*this, "Networks"));
  tabs.list.push_back(Tab::bind<GUI, &GUI::shaderTest>(*this, "WIP"));
  tabs.list.push_back(
      Tab::bind<ui::DashboardTab, &ui::DashboardTab::draw>(ui::dashboardTab(), "Dashboard"));
  tabs.list.push_back(Tab::bind<GUI, &GUI::carMap>(*this, "Map"));
};

void GUI::buildUI() {
  /* Per-Frame state updates */
  updateAvailable = updater.updateAvailable.load();
  settings.setStyle();
  setFont();
  setTabs();
  ImGui::NewFrame();
  iam_update_begin_frame();
  iam_clip_update(ImGui::GetIO().DeltaTime);

  /* Per-Frame UI building */
  titleBar.activePage = "Navigation";
  if (!tabs.list.empty() && tabs.index < tabs.list.size())
    titleBar.activePage = tabs.list[tabs.index].name;
  titleBar.draw();
  sideBar.draw(*this);
  // Keep Custom Views / plot bindings current even when those tabs are not open
  // (DBC switches bump arena generation from the sidebar).
  customViewTab().syncWithArena();
  plotManager().refreshForArena();
  canvas.draw(titleBar, sideBar, tabs);

  /* stateful UI building */
  ifKey(ImGuiKey_F3, flags.showGPUInfo, gpuGUI::buildUI, *gpu);
  ImGui::Render();
  render();
};
