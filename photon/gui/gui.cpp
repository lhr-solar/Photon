#include "gui.hpp"

#include <algorithm>
#include <csignal>
#include <cstddef>
#include <locale>
#include <string>
#include <vector>

#include "../gpu/shader.hpp"
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
#include "nucleus_frag_spv.hpp"
#include "widget.hpp"

void GUI::init(GPU& gpu, Arena& arena, Network& network) {
  this->gpu = &gpu;
  this->arena = &arena;
  this->network = &network;
  GuiSettings::regster(&settings);
  settings.setStyle();
  setTabs();
  testShader.dispatchInit(gpu, (uint32_t*)custom_shader_vert_spv, custom_shader_vert_spv_size,
                          (uint32_t*)nucleus_frag_spv, nucleus_frag_spv_size);
  // testShader.dispatchInit(gpu, (uint32_t *)custom_shader_vert_spv, custom_shader_vert_spv_size,
  // (uint32_t*)lens_frag_spv, lens_frag_spv_size);
}

void GUI::render() {
  if (!testShader.initialized.load() && testShader.partInitialized.load())
    testShader.finishInit(*gpu);
  if (testShader.showing) {
    testShader.render(*gpu, gpu->commandBuffers[gpu->frameIndex]);
    testShader.showing = false;
  }
};

void GUI::destroy() {
  if (sideBar.backgroundTexture) {
    ImGui::UnregisterUserTexture(sideBar.backgroundTexture);
    sideBar.backgroundTexture->SetStatus(ImTextureStatus_WantDestroy);
    sideBar.backgroundTexture = nullptr;
  }
  testShader.destroy();
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
  auto io = ImGui::GetIO();
  auto displaySize = io.DisplaySize;
  ImVec2 winSize = {displaySize.x * 0.50f, displaySize.y * 0.80f};
  ImVec2 winPos = {displaySize.x * 0.25f, displaySize.y * 0.10f};
  ImGui::SetNextWindowSize(winSize);
  ImGui::SetNextWindowPos(winPos);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking;

  if (ImGui::BeginPopupModal("Settings", NULL, flags)) {
    if (ImGui::Button("Exit")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
};

void GUI::updateUI() {
  auto io = ImGui::GetIO();
  auto displaySize = io.DisplaySize;
  ImVec2 winSize = {displaySize.x * 0.50f, displaySize.y * 0.80f};
  ImVec2 winPos = {displaySize.x * 0.25f, displaySize.y * 0.10f};
  ImGui::SetNextWindowSize(winSize);
  ImGui::SetNextWindowPos(winPos);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking;
  if (ImGui::BeginPopupModal("Update", nullptr, flags)) {
    ImGui::Text("Are you sure?");
    if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
};

void GUI::exportUI() {
  auto io = ImGui::GetIO();
  auto displaySize = io.DisplaySize;
  ImVec2 winSize = {displaySize.x * 0.50f, displaySize.y * 0.80f};
  ImVec2 winPos = {displaySize.x * 0.25f, displaySize.y * 0.10f};
  ImGui::SetNextWindowSize(winSize);
  ImGui::SetNextWindowPos(winPos);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking;
  if (ImGui::BeginPopupModal("Export", nullptr, flags)) {
    ImGui::Text("Are you sure?");
    if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
};

// some things:
// vertical & horizontal scaling
// auto follow + ability to scan
// better "time" label
void GUI::genericPlot(uint32_t id, uint32_t signal, ImVec2 size) {
  ImPlotSpec spec = this->settings.plotLineSpec;
  void* data = nullptr;
  void* time = nullptr;
  uint32_t timeBytes = 0;
  uint32_t dataBytes = 0;
  arena->read(id, signal, &data, &dataBytes);
  arena->readTime(id, &time, &timeBytes);
  if (!dataBytes || !timeBytes) return;
  std::string name = "##" + std::to_string(id) + std::to_string(signal);
  std::string sg_name = arena->messages[id]->signals[signal]->name;
  constexpr uint32_t maxPlotSamples = 50;
  const uint32_t sampleCount = std::min(dataBytes, timeBytes) / sizeof(double);
  const uint32_t visibleCount = std::min(sampleCount, maxPlotSamples);
  if (visibleCount == 0) return;
  const uint32_t firstSample = sampleCount - visibleCount;
  const auto* timeValues = static_cast<const double*>(time) + firstSample;
  const auto* dataValues = static_cast<const double*>(data) + firstSample;
  if (ImPlot::BeginPlot(name.data(), size)) {
    double xMin = timeValues[0];
    double xMax = timeValues[visibleCount - 1];
    if (xMax <= xMin) xMax = xMin + 1.0;
    ImPlot::SetupAxes("time", "value", 0, ImPlotAxisFlags_AutoFit);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
    ImPlot::SetupAxisLimits(ImAxis_X1, xMin, xMax, ImPlotCond_Always);
    ImPlot::PlotLine(sg_name.data(), timeValues, dataValues, static_cast<int>(visibleCount), spec);
    ImPlot::EndPlot();
  }
};

void GUI::plotTest(ImGuiWindowFlags flags) {
  if (ImGui::Begin("Page 1", NULL, flags)) {
    auto dim = ImGui::GetContentRegionAvail();
    dim.y = 0;
    genericPlot(0x7ff, 0, dim);
    genericPlot(0x7fe, 0, dim);
    genericPlot(0x7fe, 1, dim);
    genericPlot(0x7fe, 2, dim);
    genericPlot(0x7ee, 2, dim);
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
        ImGui::Image(frame.texture, drawSize);
      } else {
        ImGui::Dummy(drawSize);
      };
    } else
      ImGui::Text("loading shader");
  }
  ImGui::End();
};

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
  tabs.list.push_back(Tab::bind<GUI, &GUI::plotTest>(*this, "plot test"));
  tabs.list.push_back(Tab::bind<Arena, &Arena::statusUI>(*arena, "arena ui"));
  tabs.list.push_back(Tab::bind<GUI, &GUI::networkPage>(*this, "network page"));
  tabs.list.push_back(Tab::bind<GUI, &GUI::shaderTest>(*this, "shader test"));
};

void GUI::networkPage(ImGuiWindowFlags flags) {
  static int listIndex = 0;
  if (ImGui::Begin("network page", NULL, flags)) {
    ImGui::Combo("##NetworkCombo", &listIndex, networkOptions.data(), networkOptions.size());
    ImGui::Separator();
    if (listIndex == 0) {
      static std::string terminalText;
      static auto txReader = network->guiTxCommandBuffer.getReader();
      static TCPConfig config{
          .port = 6500,
          .ip = "3.141.38.115",
      };
      auto λ = [](ProtocolTransmitVariant& cmd) { cmd = config; };
      if (ImGui::Button("Connect To Server")) network->guiRxCommandBuffer.write(λ);
      while (ProtocolReceiveVariant* msg = txReader.read()) {
        if (auto* error = std::get_if<ProtocolError>(msg))
          terminalText += "[error] " + error->error + "\n";
        else if (auto* message = std::get_if<ProtocolMessage>(msg)) {
          terminalText += message->message + "\n";
        } else if (auto* deviceList = std::get_if<ProtocolDeviceList>(msg)) {
          terminalText += "[devices]\n";
          for (const auto& device : deviceList->devices) terminalText += "  " + device + "\n";
        }
      }
      ImGui::Separator();
      ImGui::TextUnformatted("Output");
      ImGui::BeginChild("##TcpTerminal", ImVec2(0, 220), ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_HorizontalScrollbar);
      ImGui::TextUnformatted(terminalText.c_str());
      if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
      ImGui::EndChild();
    } else if (listIndex == 1) {
      static TCPConfig config{};
      ImGui::InputText("IP", config.ip, sizeof(config.ip));
      ImGui::InputScalar("Port", ImGuiDataType_U16, &config.port);
      auto λ = [](ProtocolTransmitVariant& cmd) { cmd = config; };
      if (ImGui::Button("Submit Config")) network->guiRxCommandBuffer.write(λ);
    } else if (listIndex == 2) {
      static UDPConfig config{};
      ImGui::InputText("IP", config.ip, sizeof(config.ip));
      ImGui::InputScalar("Port", ImGuiDataType_U16, &config.port);
      ImGui::InputText("Subscription Message", config.subscribeMessage,
                       sizeof(config.subscribeMessage));
      auto λ = [](ProtocolTransmitVariant& cmd) { cmd = config; };
      if (ImGui::Button("Submit Config")) network->guiRxCommandBuffer.write(λ);
    } else if (listIndex == 3) {
      static UARTConfig config{};

      auto λ = [](ProtocolTransmitVariant& cmd) { cmd = config; };
      if (ImGui::Button("Submit Config")) network->guiRxCommandBuffer.write(λ);
    } else if (listIndex == 4) {
      static PCANConfig config{};

      auto λ = [](ProtocolTransmitVariant& cmd) { cmd = config; };
      if (ImGui::Button("Submit Config")) network->guiRxCommandBuffer.write(λ);
    } else if (listIndex == 5) {
      static BLEConfig config{};

      auto λ = [](ProtocolTransmitVariant& cmd) { cmd = config; };
      if (ImGui::Button("Submit Config")) network->guiRxCommandBuffer.write(λ);
    } else if (listIndex == 6) {
      static WLANConfig config{};

      auto λ = [](ProtocolTransmitVariant& cmd) { cmd = config; };
      if (ImGui::Button("Submit Config")) network->guiRxCommandBuffer.write(λ);
    }
  };
  ImGui::End();
};

void GUI::buildUI() {
  /* Per-Frame state updates */
  settings.setStyle();
  setFont();
  setTabs();
  ImGui::NewFrame();
  iam_update_begin_frame();
  iam_clip_update(ImGui::GetIO().DeltaTime);

  /* Per-Frame UI building */
  titleBar.draw();
  sideBar.draw(*this);
  canvas.draw(titleBar, sideBar, tabs);
  // shaderTest();

  /* stateful UI building */
  ifKey(ImGuiKey_F3, flags.showGPUInfo, gpuGUI::buildUI, *gpu);
  ImGui::Render();
  render();
};
