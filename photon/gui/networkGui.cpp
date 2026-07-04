#include "gui.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <variant>

#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "uiComponents.hpp"

namespace {

struct ProtocolOption {
  const char* name;
  const char* icon;
};

constexpr std::array<ProtocolOption, 7> kProtocols{{
    {"DAQ Server", "\uE1B0"},
    {"TCP", "\uE875"},
    {"UDP", "\uE875"},
    {"UART", "\uE8B5"},
    {"PCAN", "\uE1E6"},
    {"BLE", "\uE1A7"},
    {"WLAN", "\uE63E"},
}};

void pushInputStyle(const PhotonUi::Palette& palette) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, PhotonUi::withAlpha(palette.panel, 0.82f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, PhotonUi::withAlpha(palette.raised, 0.88f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, PhotonUi::withAlpha(palette.raised, 0.96f));
}

void popInputStyle() {
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(2);
}

void submit(Network* network, TCPConfig config) {
  network->guiRxCommandBuffer.write([config](ProtocolTransmitVariant& cmd) { cmd = config; });
}

void submit(Network* network, UDPConfig config) {
  network->guiRxCommandBuffer.write([config](ProtocolTransmitVariant& cmd) { cmd = config; });
}

void submit(Network* network, UARTConfig config) {
  network->guiRxCommandBuffer.write([config](ProtocolTransmitVariant& cmd) { cmd = config; });
}

void submit(Network* network, PCANConfig config) {
  network->guiRxCommandBuffer.write([config](ProtocolTransmitVariant& cmd) { cmd = config; });
}

void submit(Network* network, BLEConfig config) {
  network->guiRxCommandBuffer.write([config](ProtocolTransmitVariant& cmd) { cmd = config; });
}

void submit(Network* network, WLANConfig config) {
  network->guiRxCommandBuffer.write([config](ProtocolTransmitVariant& cmd) { cmd = config; });
}

void disconnect(Network* network) {
  network->guiRxCommandBuffer.write([](ProtocolTransmitVariant& cmd) { cmd = Quit{}; });
}

void drainNetworkLog(Network* network, std::string& log) {
  static auto reader = network->guiTxCommandBuffer.getReader();
  while (ProtocolReceiveVariant* msg = reader.read()) {
    if (auto* error = std::get_if<ProtocolError>(msg)) {
      log += "[error] " + error->error + "\n";
    } else if (auto* message = std::get_if<ProtocolMessage>(msg)) {
      log += message->message + "\n";
    } else if (auto* deviceList = std::get_if<ProtocolDeviceList>(msg)) {
      log += "[devices]\n";
      for (const auto& device : deviceList->devices) log += "  " + device + "\n";
    }
  }
  constexpr size_t maxLogBytes = 24000;
  if (log.size() > maxLogBytes) log.erase(0, log.size() - maxLogBytes);
}

bool protocolButton(const ProtocolOption& option, bool selected, ImVec2 size,
                    const PhotonUi::Palette& palette) {
  ImGui::PushID(option.name);
  ImGui::InvisibleButton("protocol", size);
  const bool clicked = ImGui::IsItemClicked();
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  const ImGuiID id = ImGui::GetItemID();
  const float focus =
      iam_tween_float(id, ImHashStr("focus"),
                      selected  ? 1.0f
                      : active  ? 0.88f
                      : hovered ? 0.58f
                                : 0.0f,
                      0.14f, iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade,
                      ImGui::GetIO().DeltaTime, selected ? 1.0f : 0.0f);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec4 fill = PhotonUi::mixColor(palette.raised, palette.active, focus);
  draw->AddRectFilled(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(fill, 0.84f)), 8.0f);
  draw->AddRect(min, max,
                PhotonUi::colorU32(PhotonUi::withAlpha(palette.border, 0.38f + focus * 0.28f)),
                8.0f);
  if (selected) {
    draw->AddRectFilled(min, {min.x + 4.0f, max.y}, PhotonUi::colorU32(palette.accent), 8.0f);
  }
  const ImVec2 iconSize = ImGui::CalcTextSize(option.icon);
  draw->AddText({min.x + 14.0f, min.y + (size.y - iconSize.y) * 0.5f},
                PhotonUi::colorU32(PhotonUi::mixColor(palette.muted, palette.text, focus)),
                option.icon);
  draw->AddText({min.x + 42.0f, min.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f},
                PhotonUi::colorU32(selected ? palette.text
                                            : PhotonUi::mixColor(palette.muted, palette.text, focus)),
                option.name);
  ImGui::PopID();
  return clicked;
}

void drawProtocolList(int& selected, const PhotonUi::Palette& palette) {
  const float width = ImGui::GetContentRegionAvail().x;
  for (int i = 0; i < static_cast<int>(kProtocols.size()); ++i) {
    if (protocolButton(kProtocols[i], selected == i, {width, 40.0f}, palette)) selected = i;
    ImGui::Dummy({0.0f, 4.0f});
  }
}

void drawTcpFields(TCPConfig& config, const PhotonUi::Palette& palette) {
  pushInputStyle(palette);
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("IP", config.ip, sizeof(config.ip));
  ImGui::SetNextItemWidth(140.0f);
  ImGui::InputScalar("Port", ImGuiDataType_U16, &config.port);
  popInputStyle();
}

void drawUdpFields(UDPConfig& config, const PhotonUi::Palette& palette) {
  pushInputStyle(palette);
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("IP", config.ip, sizeof(config.ip));
  ImGui::SetNextItemWidth(140.0f);
  ImGui::InputScalar("Port", ImGuiDataType_U16, &config.port);
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("Subscription", config.subscribeMessage, sizeof(config.subscribeMessage));
  popInputStyle();
}

void drawUartFields(UARTConfig& config, const PhotonUi::Palette& palette) {
  pushInputStyle(palette);
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("Device", config.device, sizeof(config.device));
  ImGui::SetNextItemWidth(180.0f);
  ImGui::InputScalar("Baud", ImGuiDataType_U32, &config.baudRate);
  popInputStyle();
}

void drawPcanFields(PCANConfig& config, const PhotonUi::Palette& palette) {
  pushInputStyle(palette);
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("Channel", config.channel, sizeof(config.channel));
  ImGui::SetNextItemWidth(140.0f);
  ImGui::InputScalar("Bitrate", ImGuiDataType_U32, &config.bitrateKbps);
  ImGui::SetNextItemWidth(140.0f);
  ImGui::InputFloat("Sample", &config.samplePointPercent);
  ImGui::Checkbox("Listen only", &config.listenOnly);
  ImGui::SameLine();
  ImGui::Checkbox("Bus reset", &config.busoffReset);
  popInputStyle();
}

void drawLogPanel(std::string& log, const PhotonUi::Palette& palette) {
  if (PhotonUi::beginPanel("##NetworkLog", {-1.0f, -1.0f}, palette)) {
    PhotonUi::label("Output", palette);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, PhotonUi::withAlpha(palette.bg, 0.32f));
    ImGui::BeginChild("##NetworkLogText", {-1.0f, -1.0f}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(log.empty() ? "" : log.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor();
  }
  PhotonUi::endPanel();
}

}  // namespace

void GUI::networkPage(ImGuiWindowFlags flags) {
  static int selected = 0;
  static std::string log;
  static TCPConfig daqConfig{.port = 6500, .ip = "3.141.38.115"};
  static TCPConfig tcpConfig{};
  static UDPConfig udpConfig{};
  static UARTConfig uartConfig{};
  static PCANConfig pcanConfig{};
  static BLEConfig bleConfig{};
  static WLANConfig wlanConfig{};

  drainNetworkLog(network, log);

  if (ImGui::Begin("Network", nullptr, flags)) {
    const PhotonUi::Palette palette = PhotonUi::palette();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float leftWidth = std::min(220.0f, std::max(160.0f, avail.x * 0.28f));
    const float rightWidth = std::max(220.0f, avail.x - leftWidth - gap);

    if (PhotonUi::beginPanel("##NetworkProtocols", {leftWidth, avail.y}, palette)) {
      PhotonUi::label("Sources", palette);
      drawProtocolList(selected, palette);
    }
    PhotonUi::endPanel();

    ImGui::SameLine(0.0f, gap);
    ImGui::BeginGroup();
    if (PhotonUi::beginPanel("##NetworkConfig", {rightWidth, std::max(180.0f, avail.y * 0.48f)},
                             palette)) {
      PhotonUi::label(kProtocols[selected].name, palette);
      if (selected == 0) {
        drawTcpFields(daqConfig, palette);
        if (PhotonUi::button("ConnectDaq", "Connect", {104.0f, 34.0f}, palette, true))
          submit(network, daqConfig);
        ImGui::SameLine(0.0f, 8.0f);
        if (PhotonUi::button("DisconnectDaq", "Disconnect", {118.0f, 34.0f}, palette))
          disconnect(network);
      } else if (selected == 1) {
        drawTcpFields(tcpConfig, palette);
        if (PhotonUi::button("ApplyTcp", "Apply", {96.0f, 34.0f}, palette, true))
          submit(network, tcpConfig);
      } else if (selected == 2) {
        drawUdpFields(udpConfig, palette);
        if (PhotonUi::button("ApplyUdp", "Apply", {96.0f, 34.0f}, palette, true))
          submit(network, udpConfig);
      } else if (selected == 3) {
        drawUartFields(uartConfig, palette);
        if (PhotonUi::button("ApplyUart", "Apply", {96.0f, 34.0f}, palette, true))
          submit(network, uartConfig);
      } else if (selected == 4) {
        drawPcanFields(pcanConfig, palette);
        if (PhotonUi::button("ApplyPcan", "Apply", {96.0f, 34.0f}, palette, true))
          submit(network, pcanConfig);
      } else if (selected == 5) {
        if (PhotonUi::button("ApplyBle", "Apply", {96.0f, 34.0f}, palette, true))
          submit(network, bleConfig);
      } else if (selected == 6) {
        if (PhotonUi::button("ApplyWlan", "Apply", {96.0f, 34.0f}, palette, true))
          submit(network, wlanConfig);
      }
    }
    PhotonUi::endPanel();

    ImGui::Dummy({0.0f, gap});
    drawLogPanel(log, palette);
    ImGui::EndGroup();
  }
  ImGui::End();
}
