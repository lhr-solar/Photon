#include "gui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <locale>
#include <string>
#include <vector>

#include "../gpu/shader.hpp"
#include "DDash/arena_bridge.hpp"
#include "DDash/dashboard.h"
#include "DDash/theme.h"
#include "arena.hpp"
#include "bits_frag_spv.hpp"
#include "box_frag_spv.hpp"
#include "config.hpp"
#include "crude_json.h"
#include "custom_shader_vert_spv.hpp"
#include "gpuGui.hpp"
#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imnodes.h"
#include "implot.h"
#include "implot3d.h"
#include "lens_frag_spv.hpp"
#include "nodes.hpp"
#include "uiComponents.hpp"
#include "widget.hpp"
#include "glowButton_frag_spv.hpp"

void GUI::init(GPU& gpu, Arena& arena, Network& network) {
  this->gpu = &gpu;
  this->arena = &arena;
  this->network = &network;
  GuiSettings::regster(&settings);
  settings.setStyle();
  setTabs();
  testShader.dispatchInit(gpu, (uint32_t*)custom_shader_vert_spv, custom_shader_vert_spv_size,
                          (uint32_t*)lens_frag_spv, lens_frag_spv_size);
  buttonShader.dispatchInit(gpu, (uint32_t*)custom_shader_vert_spv, custom_shader_vert_spv_size,
                            (uint32_t*)glowButton_frag_spv, glowButton_frag_spv_size);

#ifdef PHOTON_DASHBOARD_ONLY
  const bool dashboardOnly = true;
#else
  const bool dashboardOnly = std::getenv("PHOTON_DASHBOARD") != nullptr;
#endif
  if (dashboardOnly) {
    for (size_t i = 0; i < tabs.list.size(); ++i) {
      if (tabs.list[i].name == "Dashboard") {
        tabs.index = static_cast<int>(i);
        break;
      }
    }
    titleBar.showSidebar = false;
    titleBar.enabled = false;
    kioskMode = true;
  }
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
};

void GUI::destroy() {
  if (sideBar.backgroundTexture) {
    ImGui::UnregisterUserTexture(sideBar.backgroundTexture);
    sideBar.backgroundTexture->SetStatus(ImTextureStatus_WantDestroy);
    sideBar.backgroundTexture = nullptr;
  }
  testShader.destroy();
  buttonShader.destroy();
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
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 48.0f);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 110.0f);
    if (PhotonUi::button("CloseSettings", "Close", {96.0f, 34.0f}, palette, false, "Close"))
      ImGui::CloseCurrentPopup();
  }
  PhotonUi::endModal(open);
};

namespace {

void drawUpdateProgress(const char* id, int percentage, bool running,
                        const PhotonUi::Palette& palette) {
  const float width = ImGui::GetContentRegionAvail().x;
  const ImVec2 size(width, 64.0f);
  ImGui::InvisibleButton(id, size);

  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float rounding = 8.0f;
  const float progress = percentage < 0 ? 0.0f : std::clamp(percentage / 100.0f, 0.0f, 1.0f);
  const float animatedProgress =
      iam_tween_float(ImGui::GetItemID(), ImHashStr("update_progress"), progress, 0.18f,
                      iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade,
                      ImGui::GetIO().DeltaTime);

  draw->AddRectFilled(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.72f)),
                      rounding);
  draw->AddRect(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.border, 0.46f)),
                rounding);

  char status[64]{};
  if (percentage < 0) {
    std::snprintf(status, sizeof(status), running ? "Preparing download" : "Ready to download");
  } else {
    std::snprintf(status, sizeof(status), "%d%% downloaded", std::clamp(percentage, 0, 100));
  }

  draw->AddText({min.x + 14.0f, min.y + 10.0f}, PhotonUi::colorU32(palette.text), status);

  const ImVec2 barMin(min.x + 14.0f, min.y + 38.0f);
  const ImVec2 barMax(max.x - 14.0f, min.y + 48.0f);
  draw->AddRectFilled(barMin, barMax, PhotonUi::colorU32(PhotonUi::withAlpha(palette.bg, 0.56f)),
                      5.0f);

  if (percentage >= 0) {
    const ImVec2 fillMax(barMin.x + (barMax.x - barMin.x) * animatedProgress, barMax.y);
    if (fillMax.x > barMin.x + 1.0f) {
      draw->PushClipRect(barMin, barMax, true);
      draw->AddRectFilledMultiColor(
          barMin, fillMax, PhotonUi::colorU32(ImVec4(0.44f, 0.55f, 1.0f, 0.92f)),
          PhotonUi::colorU32(ImVec4(0.88f, 0.35f, 0.90f, 0.96f)),
          PhotonUi::colorU32(ImVec4(0.72f, 0.42f, 1.0f, 0.96f)),
          PhotonUi::colorU32(ImVec4(0.36f, 0.74f, 1.0f, 0.92f)));
      draw->PopClipRect();
    }
  } else if (running) {
    const float t = static_cast<float>(ImGui::GetTime());
    const float span = barMax.x - barMin.x;
    const float sweep = span * 0.28f;
    const float x = barMin.x + std::fmod(t * 150.0f, span + sweep) - sweep;
    draw->PushClipRect(barMin, barMax, true);
    draw->AddRectFilled({x, barMin.y}, {x + sweep, barMax.y},
                        PhotonUi::colorU32(ImVec4(0.72f, 0.42f, 1.0f, 0.55f)), 5.0f);
    draw->PopClipRect();
  }
}

void drawUpdateMeta(const char* id, const char* label, const char* value, float width,
                    const PhotonUi::Palette& palette) {
  ImGui::PushID(id);
  const ImVec2 size(width, 58.0f);
  ImGui::InvisibleButton("meta", size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.56f)),
                      8.0f);
  draw->AddRect(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.border, 0.34f)), 8.0f);
  draw->AddText({min.x + 12.0f, min.y + 10.0f}, PhotonUi::colorU32(palette.muted), label);
  draw->PushClipRect({min.x + 12.0f, min.y + 31.0f}, {max.x - 12.0f, max.y - 8.0f}, true);
  draw->AddText({min.x + 12.0f, min.y + 32.0f}, PhotonUi::colorU32(palette.text), value);
  draw->PopClipRect();
  ImGui::PopID();
}

}  // namespace

void GUI::updateUI() {
  const bool open = PhotonUi::beginModal("Update", {540.0f, 350.0f});
  if (open) {
    const PhotonUi::Palette palette = PhotonUi::palette();
    const bool running = updater.running.load();
    const int downloadPercentage = updater.photonDownloadPercentage.load();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 headerMin = ImGui::GetCursorScreenPos();
    const ImVec2 iconMax(headerMin.x + 42.0f, headerMin.y + 42.0f);
    draw->AddRectFilled(headerMin, iconMax,
                        PhotonUi::colorU32(PhotonUi::withAlpha(palette.active, 0.72f)), 8.0f);
    PhotonUi::drawIconCentered(draw, "\ueb13", headerMin, iconMax, 24.0f,
                               PhotonUi::colorU32(palette.text), -1.0f);
    draw->AddText({headerMin.x + 54.0f, headerMin.y + 2.0f}, PhotonUi::colorU32(palette.text),
                  "Update available");
    draw->AddText({headerMin.x + 54.0f, headerMin.y + 25.0f}, PhotonUi::colorU32(palette.muted),
                  running ? "Downloading the latest Photon build"
                          : "A newer build is ready to download");
    ImGui::Dummy({1.0f, 54.0f});

    drawUpdateProgress("PhotonUpdateProgress", downloadPercentage, running, palette);

    ImGui::Spacing();
    const float metaWidth = (ImGui::GetContentRegionAvail().x - 10.0f) * 0.5f;
    drawUpdateMeta("VersionMeta", "Version", updater.version.c_str(), metaWidth, palette);
    ImGui::SameLine(0.0f, 10.0f);
    drawUpdateMeta("PackageMeta", "Package", "Photon.exe", metaWidth, palette);

    const float bottomY = ImGui::GetWindowHeight() - 52.0f;
    ImGui::SetCursorPosY(bottomY);
    const float closeW = 96.0f;
    const float downloadW = 150.0f;
    const float gap = 10.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - closeW - downloadW - gap - 14.0f);
    ImGui::BeginDisabled(running);
    if (PhotonUi::button("Download", running ? "Downloading" : "Download", {downloadW, 34.0f},
                         palette, false, "Download update")) {
      updater.launchUpdater();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, gap);
    if (PhotonUi::button("CloseUpdate", "Close", {96.0f, 34.0f}, palette, false, "Close"))
      ImGui::CloseCurrentPopup();
  }
  PhotonUi::endModal(open);
};

void GUI::exportUI() {
  const bool open = PhotonUi::beginModal("Export", {420.0f, 220.0f});
  if (open) {
    const PhotonUi::Palette palette = PhotonUi::palette();
    PhotonUi::label("Export", palette);
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 48.0f);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 110.0f);
    if (PhotonUi::button("CloseExport", "Close", {96.0f, 34.0f}, palette, false, "Close"))
      ImGui::CloseCurrentPopup();
  }
  PhotonUi::endModal(open);
};

void GUI::genericPlot(uint32_t id, uint32_t signal, ImVec2 size) {
  ImPlotSpec spec = this->settings.plotLineSpec;
  void* data = nullptr;
  void* time = nullptr;
  uint32_t timeBytes = 0;
  uint32_t dataBytes = 0;
  arena->read(id, signal, &data, &dataBytes);
  arena->readTime(id, &time, &timeBytes);
  if (!dataBytes || !timeBytes) return;
  char name[64];
  std::snprintf(name, sizeof(name), "##%u_%u", id, signal);
  const char* signalName = arena->messages[id]->signals[signal]->name.c_str();
  constexpr uint32_t maxPlotSamples = 100;
  const uint32_t sampleCount = std::min(dataBytes, timeBytes) / sizeof(double);
  const uint32_t visibleCount = std::min(sampleCount, maxPlotSamples);
  if (visibleCount == 0) return;
  const uint32_t firstSample = sampleCount - visibleCount;
  const auto* timeValues = static_cast<const double*>(time) + firstSample;
  const auto* dataValues = static_cast<const double*>(data) + firstSample;
  if (ImPlot::BeginPlot(name, size)) {
    ImPlot::SetupAxes("time", "value", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
    ImPlot::PlotLine(signalName, timeValues, dataValues, static_cast<int>(visibleCount), spec);
    ImPlot::EndPlot();
  }
};

void GUI::plotTest(ImGuiWindowFlags flags) {
  if (ImGui::Begin("Page 1", NULL, flags)) {
    auto dim = ImGui::GetContentRegionAvail();
    dim.y = 0;
    for (const uint32_t id : arena->validIds) {
      if (id >= arena->messages.size() || !arena->messages[id]) continue;
      for (uint32_t signal = 0; signal < arena->messages[id]->signalCount; signal++)
        genericPlot(id, signal, dim);
    }
  }
  ImGui::End();
};

VkExtent2D quantizeContentExtent(ImVec2 contentSize, VkExtent2D fallback) {
  if (contentSize.x <= 1.0f || contentSize.y <= 1.0f) return fallback;
  const uint32_t width = std::max(1u, static_cast<uint32_t>(std::lround(contentSize.x)));
  const uint32_t height = std::max(1u, static_cast<uint32_t>(std::lround(contentSize.y)));
  return {width, height};
}

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
  tabs.list.clear();
#ifndef PHOTON_DASHBOARD_ONLY
  tabs.list.push_back(Tab::bind<GUI, &GUI::plotTest>(*this, "Plots"));
  tabs.list.push_back(Tab::bind<Arena, &Arena::statusUI>(*arena, "Arena"));
  tabs.list.push_back(Tab::bind<GUI, &GUI::networkPage>(*this, "Networks"));
  tabs.list.push_back(Tab::bind<GUI, &GUI::shaderTest>(*this, "WIP"));
#endif
  tabs.list.push_back(Tab::bind<GUI, &GUI::dashboardPage>(*this, "Dashboard"));
};

void GUI::dashboardPage(ImGuiWindowFlags) {
  ui::UpdateDashboardState(*arena, dashboardState);
  ui::RenderDashboard(dashboardState);
}

void GUI::buildUI() {
  /* Per-Frame state updates */
  updateAvailable = true;
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
  canvas.draw(titleBar, sideBar, tabs);

  /* stateful UI building */
  if (!kioskMode) ifKey(ImGuiKey_F3, flags.showGPUInfo, gpuGUI::buildUI, *gpu);
  ImGui::Render();
  render();
};
