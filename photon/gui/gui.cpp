#include "gui.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#if PHOTON_GUI_RENDER_ITEMS
#include "../gpu/shader.hpp"
#include "custom_shader_vert_spv.hpp"
#include "glowButton_frag_spv.hpp"
#include "lens_frag_spv.hpp"
#include "newCar_glb.hpp"
#include "track_glb.hpp"
#endif
#include "DDash/dashboard_tab.h"
#include "arena.hpp"
#include "config.hpp"
#if defined(APPLE) || defined(__APPLE__)
#include "gpuMetalGui.hpp"
#include "imgui_impl_sdl3.h"
#else
#include "gpuGui.hpp"
#endif
#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imnodes.h"
#include "implot.h"
#include "plots.hpp"
#include "uiComponents.hpp"
#include "widget.hpp"

namespace {
#if PHOTON_GUI_RENDER_ITEMS
struct FrameView {
  const Message* message{};
  const double* times{};
  uint32_t count{};
  uint32_t index{};

  double time(int offset = 0) const { return times[index + offset]; }
  double value(uint32_t signal, int offset = 0) const {
    return static_cast<const double*>(message->signals[signal]->data)[index + offset];
  }
};

bool frameAt(const Arena& arena, uint32_t id, double cursor, FrameView& frame) {
  if (id >= arena.messages.size() || !arena.messages[id]) return false;
  const Message& message = *arena.messages[id];
  const uint32_t count = message.signalSize.value.load(std::memory_order_acquire) / sizeof(double);
  if (!count) return false;
  const auto* times = static_cast<const double*>(message.timeData);
  const double* found = std::upper_bound(times, times + count, cursor);
  if (found == times) return false;
  frame = {&message, times, count, static_cast<uint32_t>(--found - times)};
  return true;
}

Position gpsPosition(double latitude, double longitude) {
  // Brainerd OSM centerline fitted to the Assetto Corsa track.glb coordinate system.
  constexpr double latitudeOrigin = 46.41613092075472;
  constexpr double longitudeOrigin = -94.27538689056604;
  constexpr double metersPerLatitudeDegree = 111320.0;
  constexpr double metersPerLongitudeDegree = 76745.74845956931;
  constexpr double eastX = 0.09952096955160591;
  constexpr double northX = 0.00019804275620237066;
  constexpr double eastY = -0.00019804275620237066;
  constexpr double northY = 0.09952096955160591;
  const double east = (longitude - longitudeOrigin) * metersPerLongitudeDegree;
  const double north = (latitude - latitudeOrigin) * metersPerLatitudeDegree;
  return {static_cast<float>(eastX * east + northX * north - 3.75936219),
          static_cast<float>(eastY * east + northY * north + 4.97846090), 0.3f};
}

struct GpsSpan {
  Position previous{};
  Position current{};
  Position next{};
  double previousTime{};
  double time{};
  double nextTime{};
  bool hasPrevious{};
  bool hasNext{};
};

bool gpsPoint(const FrameView& latitude, const FrameView& longitude, int offset, Position& position,
              double& time) {
  const int latitudeIndex = static_cast<int>(latitude.index) + offset;
  const int longitudeIndex = static_cast<int>(longitude.index) + offset;
  if (latitudeIndex < 0 || longitudeIndex < 0 || latitudeIndex >= static_cast<int>(latitude.count) ||
      longitudeIndex >= static_cast<int>(longitude.count) ||
      std::abs(latitude.times[latitudeIndex] - longitude.times[longitudeIndex]) > 1.0)
    return false;
  const double lat = latitude.value(0, offset);
  const double lon = longitude.value(0, offset);
  if (!std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat) > 90.0 ||
      std::abs(lon) > 180.0 || (lat == 0.0 && lon == 0.0))
    return false;
  position = gpsPosition(lat, lon);
  time = std::max(latitude.times[latitudeIndex], longitude.times[longitudeIndex]);
  return true;
}

bool gpsSpanAt(const Arena& arena, double cursor, GpsSpan& gps) {
  FrameView latitude{}, longitude{};
  if (!frameAt(arena, 4288, cursor, latitude) || !frameAt(arena, 4289, cursor, longitude) ||
      !gpsPoint(latitude, longitude, 0, gps.current, gps.time))
    return false;
  gps.hasPrevious = gpsPoint(latitude, longitude, -1, gps.previous, gps.previousTime);
  gps.hasNext = gpsPoint(latitude, longitude, 1, gps.next, gps.nextTime);
  return true;
}

float distance(Position a, Position b) { return std::hypot(b.x - a.x, b.y - a.y); }
float direction(Position a, Position b) { return std::atan2(b.y - a.y, b.x - a.x); }

bool fusedMapPosition(const Arena& arena, double cursor, GUI::MapTracker& tracker,
                      Position& position, float& heading) {
  GpsSpan gps{};
  if (!gpsSpanAt(arena, cursor, gps)) return false;

  if (gps.hasNext && gps.nextTime > gps.time) {
    const float amount = static_cast<float>(std::clamp((cursor - gps.time) /
                                                           (gps.nextTime - gps.time),
                                                       0.0, 1.0));
    position = {std::lerp(gps.current.x, gps.next.x, amount),
                std::lerp(gps.current.y, gps.next.y, amount), 0.3f};
    const float segmentDistance = distance(gps.current, gps.next);
    heading = segmentDistance > 0.05f ? direction(gps.current, gps.next) : tracker.heading;
    tracker = {position, cursor, gps.time,
               segmentDistance / static_cast<float>(gps.nextTime - gps.time), heading, true};
    return true;
  }

  const bool reset = !tracker.valid || cursor < tracker.time || cursor - tracker.time > 0.5 ||
                     gps.time > tracker.gpsTime;
  if (reset) {
    tracker.position = gps.current;
    tracker.time = gps.time;
    tracker.gpsTime = gps.time;
    if (gps.hasPrevious && gps.time > gps.previousTime) {
      const float segmentDistance = distance(gps.previous, gps.current);
      if (segmentDistance > 0.05f) tracker.heading = direction(gps.previous, gps.current);
      tracker.speed = segmentDistance / static_cast<float>(gps.time - gps.previousTime);
    }
    tracker.valid = true;
  }

  const float dt = static_cast<float>(std::clamp(cursor - tracker.time, 0.0, 30.0));
  FrameView velocity{}, acceleration{}, gyro{};
  const bool hasVelocity = frameAt(arena, 1059, cursor, velocity) &&
                           velocity.message->signalCount > 1 && cursor - velocity.time() < 1.0 &&
                           std::isfinite(velocity.value(1));
  const bool hasAcceleration = frameAt(arena, 4528, cursor, acceleration) &&
                               acceleration.message->signalCount > 2 &&
                               cursor - acceleration.time() < 0.25;
  const bool hasGyro = frameAt(arena, 4529, cursor, gyro) && gyro.message->signalCount > 2 &&
                       cursor - gyro.time() < 0.25;

  float yawRate = 0.0f;
  if (hasAcceleration && hasGyro && std::abs(acceleration.time() - gyro.time()) < 0.1) {
    // Project angular velocity onto measured gravity, independent of the IMU's mounting axes.
    const float ax = static_cast<float>(acceleration.value(0));
    const float ay = static_cast<float>(acceleration.value(1));
    const float az = static_cast<float>(acceleration.value(2));
    const float gravity = std::sqrt(ax * ax + ay * ay + az * az);
    if (gravity > 0.7f && gravity < 1.3f)
      yawRate = static_cast<float>((gyro.value(0) * ax + gyro.value(1) * ay +
                                    gyro.value(2) * az) /
                                   gravity) *
                std::numbers::pi_v<float> / 180.0f;
  }

  if (dt > 0.0f && (hasVelocity || (hasAcceleration && hasGyro))) {
    const float nextHeading = tracker.heading + std::clamp(yawRate, -4.0f, 4.0f) * dt;
    float nextSpeed = tracker.speed;
    if (hasVelocity) {
      constexpr float metersToScene = 0.099521f;
      const float measured = std::clamp(std::abs(static_cast<float>(velocity.value(1))), 0.0f,
                                        100.0f) *
                             metersToScene;
      nextSpeed = std::lerp(nextSpeed, measured, 1.0f - std::exp(-8.0f * dt));
    }
    const float travel = (tracker.speed + nextSpeed) * 0.5f * dt;
    const float middleHeading = (tracker.heading + nextHeading) * 0.5f;
    tracker.position.x += std::cos(middleHeading) * travel;
    tracker.position.y += std::sin(middleHeading) * travel;
    tracker.position.z = 0.3f;
    tracker.heading = nextHeading;
    tracker.speed = nextSpeed;
  }
  tracker.time = cursor;
  position = tracker.position;
  heading = tracker.heading;
  return true;
}
#endif

void drawFpsOverlay() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      {viewport->WorkPos.x + viewport->WorkSize.x - 10, viewport->WorkPos.y + 10}, ImGuiCond_Always,
      {1, 0});
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs |
      ImGuiWindowFlags_NoBackground;
  if (ImGui::Begin("FPS Overlay", nullptr, flags))
    ImGui::Text("%.0f FPS", ImGui::GetIO().Framerate);
  ImGui::End();
}
}  // namespace

void GUI::init(GPU& gpu, Arena& arena, Network& network) {
  this->gpu = &gpu;
  this->arena = &arena;
  this->network = &network;
  GuiSettings::regster(&settings);
  settings.setStyle();
  setTabs();
  updater.queryReleaseInfoOnceAsync();
#if PHOTON_GUI_RENDER_ITEMS
  testShader.dispatchInit(gpu, (uint32_t*)custom_shader_vert_spv, custom_shader_vert_spv_size,
                          (uint32_t*)lens_frag_spv, lens_frag_spv_size);
  buttonShader.dispatchInit(gpu, (uint32_t*)custom_shader_vert_spv, custom_shader_vert_spv_size,
                            (uint32_t*)glowButton_frag_spv, glowButton_frag_spv_size);

  scene.addModel("track_glb", track_glb, track_glb_size, false);
  scene.addModel("newCar_glb", newCar_glb, newCar_glb_size, true);
  scene.dispatchInit(gpu);
#endif
}

void GUI::render() {
#if PHOTON_GUI_RENDER_ITEMS
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
#endif
};

void GUI::destroy() {
  videoUi.stop();
  if (videoUi.videoTexture.Status != ImTextureStatus_Destroyed) {
    ImGui::UnregisterUserTexture(&videoUi.videoTexture);
    videoUi.videoTexture.SetStatus(ImTextureStatus_WantDestroy);
  }
  if (sideBar.backgroundTexture) {
    ImGui::UnregisterUserTexture(sideBar.backgroundTexture);
    sideBar.backgroundTexture->SetStatus(ImTextureStatus_WantDestroy);
    sideBar.backgroundTexture = nullptr;
  }
#if PHOTON_GUI_RENDER_ITEMS
  testShader.destroy();
  buttonShader.destroy();
  scene.destroy();
#endif
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
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 18.0f);
    const float actionWidth = ImGui::GetContentRegionAvail().x;
    const bool exporting = exporter.running.load();
    if (PhotonUi::rowButton("ExportCsv", "\uede9",
                            exporting ? "Exporting output.csv" : "Export output.csv",
                            {actionWidth, 42.0f}, palette, false, exporting)) {
      std::thread t(&Exporter::toFile, &exporter, std::ref(*arena), "output.csv");
      t.detach();
    }
    if (PhotonUi::modalCloseButton("CloseExport", palette)) ImGui::CloseCurrentPopup();
  }
  PhotonUi::endModal(open);
};

void GUI::plotTest(ImGuiWindowFlags flags) {
  flags &= ~(ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  flags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
  if (ImGui::Begin("Page 1", NULL, flags)) {
    ArenaReadScope read(*arena);
    auto dim = ImGui::GetContentRegionAvail();
    dim.y = 0;
    ImPlotSpec spec = settings.plotLineSpec;
    for (const uint32_t id : arena->validIds) {
      if (id >= arena->messages.size() || !arena->messages[id]) continue;
      for (uint32_t signal = 0; signal < arena->messages[id]->signalCount; signal++)
        plots.signal(*arena, id, signal, dim, spec);
    }
  }
  ImGui::End();
};

void GUI::carMap(ImGuiWindowFlags flags) {
#if PHOTON_GUI_RENDER_ITEMS
  scene.showing = false;
  const bool ready =
      scene.initialized.load() && !scene.frames.empty() && scene.frameIndex != nullptr;
  SceneFrame fallbackFrame{};
  SceneFrame& frame = ready ? scene.frames[*scene.frameIndex] : fallbackFrame;
  const PhotonUi::Palette palette = PhotonUi::palette();

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
      if (hasTrackedObject) {
        Position nextPosition{};
        float nextHeading{};
        const bool smooth = mapTracker.valid && plots.cursor >= mapTracker.time &&
                            plots.cursor - mapTracker.time < 0.5;
        ArenaReadScope read(*arena);
        if (fusedMapPosition(*arena, plots.cursor, mapTracker, nextPosition, nextHeading)) {
          SceneObject& car = scene.objects[scene.trackedObjectIndex];
          const float amount = smooth ? 1.0f - std::exp(-10.0f * ImGui::GetIO().DeltaTime) : 1.0f;
          car.position.x = std::lerp(car.position.x, nextPosition.x, amount);
          car.position.y = std::lerp(car.position.y, nextPosition.y, amount);
          car.position.z = nextPosition.z;
          const float rotation = nextHeading * 180.0f / std::numbers::pi_v<float> + 90.0f;
          car.rotationDegrees += std::remainder(rotation - car.rotationDegrees, 360.0f) * amount;
        }
      }
      const Position trackedPosition =
          hasTrackedObject ? scene.objects[scene.trackedObjectIndex].position : Position{};

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
                              PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.88f)),
                              PhotonUi::kFrameRounding);
      drawList->AddRect(textMin, textMax,
                        PhotonUi::colorU32(PhotonUi::withAlpha(palette.border, 0.42f)),
                        PhotonUi::kFrameRounding);
      drawList->AddText(ImVec2(textMin.x + textPadding.x, textMin.y + textPadding.y),
                        PhotonUi::colorU32(palette.text), positionLabel);

      ImGuiIO& io = ImGui::GetIO();
      if (sceneHovered) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
          scene.camera.yaw -= io.MouseDelta.x * scene.camera.orbitSensitivity;
          scene.camera.pitch += io.MouseDelta.y * scene.camera.orbitSensitivity;
          scene.camera.pitch = std::clamp(scene.camera.pitch, -89.0f, 89.0f);
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
#else
  (void)flags;
#endif
};

void GUI::shaderTest(ImGuiWindowFlags flags) {
#if PHOTON_GUI_RENDER_ITEMS
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
#else
  (void)flags;
#endif
};

void GUI::drawButtonShaderOverlay(ImVec2 buttonMin, ImVec2 buttonMax) {
#if PHOTON_GUI_RENDER_ITEMS
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
#else
  (void)buttonMin;
  (void)buttonMax;
#endif
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
  tabs.list.clear();
  tabs.list.push_back(Tab::bind<GUI, &GUI::plotTest>(*this, "Plots"));
  tabs.list.push_back(Tab::bind<Arena, &Arena::statusUI>(*arena, "Arena"));
  tabs.list.push_back(Tab::bind<GUI, &GUI::networkPage>(*this, "Networks"));
  tabs.list.push_back(
      Tab::bind<ui::DashboardTab, &ui::DashboardTab::draw>(ui::dashboardTab(), "Dashboard"));
#if PHOTON_GUI_RENDER_ITEMS
  tabs.list.push_back(Tab::bind<GUI, &GUI::carMap>(*this, "Map"));
#endif
  tabs.list.push_back(Tab::bind<VideoUI, &VideoUI::videoController>(videoUi, "Livestream"));
#if PHOTON_GUI_RENDER_ITEMS
  tabs.list.push_back(Tab::bind<GUI, &GUI::shaderTest>(*this, "WIP"));
#endif
};

void GUI::buildUI() {
  /* Per-Frame state updates */
  updateAvailable = updater.updateAvailable.load();
  settings.setStyle();
  setFont();
  setTabs();
#if defined(APPLE) || defined(__APPLE__)
  ImGui_ImplSDL3_NewFrame();
#endif
  ImGui::NewFrame();
  iam_update_begin_frame();
  iam_clip_update(ImGui::GetIO().DeltaTime);

  /* Per-Frame UI building */
  updateNetworkStatus();
  titleBar.activePage = "Navigation";
  if (!tabs.list.empty() && tabs.index < tabs.list.size())
    titleBar.activePage = tabs.list[tabs.index].name;
  titleBar.draw();
  sideBar.draw(*this);
  canvas.draw(titleBar, sideBar, tabs, titleBar.height);
  plots.timeline(*arena, network,
                 titleBar.connectionConnected && titleBar.connectionProtocol == "DAQ Server",
                 {canvas.pos.x, canvas.pos.y + canvas.size.y}, {canvas.size.x, titleBar.height});

  /* stateful UI building */
  ifKey(ImGuiKey_F3, flags.showFPS, drawFpsOverlay);
  ImGui::Render();
  render();
};
