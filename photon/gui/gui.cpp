#include "gui.hpp"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <locale>
#include <string>
#include <vector>

#include "../gpu/shader.hpp"
#include "DDash/dashboard_tab.h"
#include "arena.hpp"
#include "bits_frag_spv.hpp"
#include "box_frag_spv.hpp"
#include "config.hpp"
#include "crude_json.h"
#include "custom_shader_vert_spv.hpp"
#include "glowButton_frag_spv.hpp"
#include "gpuGui.hpp"
#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imnodes.h"
#include "implot.h"
#include "implot3d.h"
#include "lens_frag_spv.hpp"
#include "nodes.hpp"
#include "nucleus_frag_spv.hpp"
#include "uiComponents.hpp"
#include "exportPanel.hpp"
#include "recorderSettings.hpp"
#include "replayPanel.hpp"
#include "widget.hpp"

void GUI::init(GPU& gpu, Arena& arena, Network& network) {
  this->gpu = &gpu;
  this->arena = &arena;
  this->network = &network;
  GuiSettings::regster(&settings);
  settings.setStyle();
  setTabs();
  updater.queryReleaseInfoOnceAsync();
  testShader.dispatchInit(gpu, (uint32_t*)custom_shader_vert_spv, custom_shader_vert_spv_size,
                          (uint32_t*)lens_frag_spv, lens_frag_spv_size);
  buttonShader.dispatchInit(gpu, (uint32_t*)custom_shader_vert_spv, custom_shader_vert_spv_size,
                            (uint32_t*)glowButton_frag_spv, glowButton_frag_spv_size);
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

void GUI::updateUI() { updater.drawUI(updateAvailable); };

void GUI::exportUI() {
  const bool open = PhotonUi::beginModal("Export", {480.0f, 520.0f});
  if (open) {
    const PhotonUi::Palette palette = PhotonUi::palette();
    PhotonUi::label("Export", palette);
    ImGui::Dummy({0.0f, 6.0f});
    gui::drawExportPanel(*arena, gpu->window, recorder, &replayController);
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

void GUI::replayTransportWindow() {
  if (!replayController.isLoaded()) return;

  const auto   status    = replayController.status();
  const bool   isPlaying = (status.state == io::ReplayState::Playing);
  const PhotonUi::Palette palette = PhotonUi::palette();
  const float  gap       = ImGui::GetStyle().ItemSpacing.x;

  ImGui::SetNextWindowSize({460.0f, 112.0f}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints({320.0f, 112.0f}, {900.0f, 112.0f});

  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(
    {(io.DisplaySize.x - 460.0f) * 0.5f, io.DisplaySize.y - 160.0f},
    ImGuiCond_FirstUseEver);

  // Bring to front only once when replay first loads
  static bool s_bringToFront = false;
  static bool s_wasLoaded    = false;
  const bool  nowLoaded      = replayController.isLoaded();
  if (nowLoaded && !s_wasLoaded) { s_bringToFront = true; }
  s_wasLoaded = nowLoaded;
  if (s_bringToFront) {
    ImGui::SetNextWindowFocus();
    s_bringToFront = false;
  }

  bool open = true;
  ImGuiWindowFlags wflags = ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse |
                            ImGuiWindowFlags_NoCollapse;
  if (ImGui::Begin("Replay###ReplayTransport", &open, wflags)) {

    // ── Row 1: Play  Stop  |  -  speed  +  ─────────────────────────────
    if (PhotonUi::button("RT_Play", isPlaying ? "Pause" : "Play",
                         {60.0f, 28.0f}, palette, isPlaying)) {
      if (isPlaying) replayController.pause();
      else           replayController.play();
    }
    ImGui::SameLine(0.0f, gap);
    if (PhotonUi::button("RT_Stop", "Stop", {52.0f, 28.0f}, palette))
      replayController.stop();

    ImGui::SameLine(0.0f, gap * 2.0f);
    {
      ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddLine(
        {p.x, p.y + 3.0f}, {p.x, p.y + 25.0f},
        IM_COL32(90, 90, 90, 180), 1.0f);
      ImGui::Dummy({1.0f, 28.0f});
    }
    ImGui::SameLine(0.0f, gap * 2.0f);

    static float s_speed = 1.0f;
    if (PhotonUi::button("RT_SpeedDn", "-", {28.0f, 28.0f}, palette)) {
      s_speed = std::max(0.25f, s_speed - 0.25f);
      replayController.setSpeed(s_speed);
    }
    ImGui::SameLine(0.0f, gap);
    // Show speed as "1.00x" centred in a fixed-width slot
    char speedBuf[12];
    std::snprintf(speedBuf, sizeof(speedBuf), "%.2fx", s_speed);
    ImGui::SetNextItemWidth(52.0f);
    ImGui::TextUnformatted(speedBuf);
    ImGui::SameLine(0.0f, gap);
    if (PhotonUi::button("RT_SpeedUp", "+", {28.0f, 28.0f}, palette)) {
      s_speed = std::min(10.0f, s_speed + 0.25f);
      replayController.setSpeed(s_speed);
    }

    ImGui::Spacing();

    // ── Row 2: time label centred, then full-width slider ────────────────
    const double recStart = status.startTime;
    double el             = status.playheadTime - recStart;
    double dur            = status.duration > 0.0 ? status.duration : 1.0;
    double elMin          = 0.0;

    auto fmtEl = [](char* b, int n, double s) {
      if (s < 0.0) s = 0.0;
      int m = static_cast<int>(s) / 60;
      double sec = s - m * 60.0;
      std::snprintf(b, n, "%d:%06.3f", m, sec);
    };
    char elBuf[16], durBuf[16], labelBuf[40];
    fmtEl(elBuf,  sizeof(elBuf),  el);
    fmtEl(durBuf, sizeof(durBuf), dur);
    std::snprintf(labelBuf, sizeof(labelBuf), "%s / %s", elBuf, durBuf);

    // Slider — empty format so grab is clean; label drawn separately above
    const float availW = ImGui::GetContentRegionAvail().x;
    const ImVec2 tSz   = ImGui::CalcTextSize(labelBuf);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - tSz.x) * 0.5f);
    ImGui::TextUnformatted(labelBuf);

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderScalar("##RT_seek", ImGuiDataType_Double,
                            &el, &elMin, &dur, ""))
      replayController.seek(recStart + el);

    // Fault markers
    const auto& faults = replayController.faultTimestamps();
    if (!faults.empty() && dur > 0.0) {
      const ImVec2 sMin = ImGui::GetItemRectMin();
      const ImVec2 sMax = ImGui::GetItemRectMax();
      ImDrawList*  dl   = ImGui::GetWindowDrawList();
      for (double ft : faults) {
        const double t = ft - recStart;
        if (t < 0.0 || t > dur) continue;
        const float frac = static_cast<float>(t / dur);
        const float x    = sMin.x + frac * (sMax.x - sMin.x);
        dl->AddLine({x, sMin.y}, {x, sMax.y}, IM_COL32(220, 40, 40, 220), 2.0f);
      }
    }
  }
  ImGui::End();

  if (!open)
    replayController.stop();
}

void GUI::replayPage(ImGuiWindowFlags flags) {
  if (ImGui::Begin("Replay", nullptr, flags)) {
    gui::drawReplayPanel(replayController, *arena, network, gpu->window, recorder);
  }
  ImGui::End();
};

void GUI::setTabs() {
  tabs.list.clear();
  tabs.list.push_back(Tab::bind<GUI, &GUI::plotTest>(*this, "Plots"));
  tabs.list.push_back(Tab::bind<Arena, &Arena::statusUI>(*arena, "Arena"));
  tabs.list.push_back(Tab::bind<GUI, &GUI::networkPage>(*this, "Networks"));
  tabs.list.push_back(Tab::bind<GUI, &GUI::shaderTest>(*this, "WIP"));
  tabs.list.push_back(
      Tab::bind<ui::DashboardTab, &ui::DashboardTab::draw>(ui::dashboardTab(), "Dashboard"));
  tabs.list.push_back(Tab::bind<GUI, &GUI::replayPage>(*this, "Replay"));
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
  titleBar.recorder          = recorder;
  titleBar.replayController  = &replayController;
  titleBar.draw();
  sideBar.draw(*this);
  canvas.draw(titleBar, sideBar, tabs);

  /* stateful UI building */
  replayTransportWindow();
  ifKey(ImGuiKey_F3, flags.showGPUInfo, gpuGUI::buildUI, *gpu);
  ImGui::Render();
  render();
};
