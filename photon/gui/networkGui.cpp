#include <algorithm>
#include <array>
#include <string>
#include <variant>

#include "gui.hpp"
#include "imgui.h"
#include "uiComponents.hpp"

namespace {

struct ProtocolOption {
  const char* name;
  const char* icon;
};

constexpr std::array<ProtocolOption, 7> kProtocols{{
    {"DAQ Server", "\ueb1f"},
    {"TCP", "\uf09f"},
    {"UDP", "\ueb17"},
    {"UART", "\uf00c"},
    {"PCAN", "\uef8e"},
    {"BLE", "\uea37"},
    {"WLAN", "\ueb52"},
}};
constexpr TCPConfig kDaqConfig{
    .port = 6500,
    .ip = "3.141.38.115",
    .tag = 1,
};
std::string networkLog;

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

void setConnectionPending(TitleBar& titleBar, const char* protocol) {
  titleBar.connectionActive = true;
  titleBar.connectionConnected = false;
  titleBar.connectionFailed = false;
  titleBar.connectionProtocol = protocol;
}

void setConnectionOffline(TitleBar& titleBar) {
  titleBar.connectionActive = false;
  titleBar.connectionConnected = false;
  titleBar.connectionFailed = false;
  titleBar.connectionProtocol = "Offline";
}

void setConnectionFailed(TitleBar& titleBar) {
  titleBar.connectionActive = false;
  titleBar.connectionConnected = false;
  titleBar.connectionFailed = true;
  titleBar.connectionProtocol = "Offline";
}

void drainNetworkLog(Network* network, std::string& log, TitleBar& titleBar) {
  static auto reader = network->guiTxCommandBuffer.getReader();
  while (ProtocolReceiveVariant* msg = reader.read()) {
    if (auto* error = std::get_if<ProtocolError>(msg)) {
      log += "[error] " + error->error + "\n";
      if (titleBar.connectionActive && !titleBar.connectionConnected &&
          titleBar.connectionProtocol == "DAQ Server")
        setConnectionFailed(titleBar);
      else
        setConnectionOffline(titleBar);
    } else if (auto* message = std::get_if<ProtocolMessage>(msg)) {
      log += message->message + "\n";
      if (message->message.find("TCP connected") != std::string::npos) {
        titleBar.connectionActive = true;
        titleBar.connectionConnected = true;
        titleBar.connectionFailed = false;
      } else if (message->message.find("TCP peer closed connection") != std::string::npos ||
                 (titleBar.connectionConnected &&
                  message->message.find("TCP stopped") != std::string::npos)) {
        setConnectionOffline(titleBar);
      }
    } else if (auto* deviceList = std::get_if<ProtocolDeviceList>(msg)) {
      log += "[devices]\n";
      for (const auto& device : deviceList->devices) log += "  " + device + "\n";
    }
  }
  constexpr size_t maxLogBytes = 24000;
  if (log.size() > maxLogBytes) log.erase(0, log.size() - maxLogBytes);
}

void drawProtocolList(int& selected, const PhotonUi::Palette& palette) {
  const float width = ImGui::GetContentRegionAvail().x;
  for (int i = 0; i < static_cast<int>(kProtocols.size()); ++i) {
    if (PhotonUi::rowButton(kProtocols[i].name, kProtocols[i].icon, kProtocols[i].name,
                            {width, 40.0f}, palette, selected == i))
      selected = i;
    ImGui::Dummy({0.0f, 4.0f});
  }
}

void drawTcpFields(TCPConfig& config, const PhotonUi::Palette& palette) {
  PhotonUi::pushInputStyle(palette);
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("IP", config.ip, sizeof(config.ip));
  ImGui::SetNextItemWidth(140.0f);
  ImGui::InputScalar("Port", ImGuiDataType_U16, &config.port);
  PhotonUi::popInputStyle();
}

void drawUdpFields(UDPConfig& config, const PhotonUi::Palette& palette) {
  PhotonUi::pushInputStyle(palette);
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("IP", config.ip, sizeof(config.ip));
  ImGui::SetNextItemWidth(140.0f);
  ImGui::InputScalar("Port", ImGuiDataType_U16, &config.port);
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("Subscription", config.subscribeMessage, sizeof(config.subscribeMessage));
  PhotonUi::popInputStyle();
}

void drawUartFields(UARTConfig& config, const PhotonUi::Palette& palette) {
  PhotonUi::pushInputStyle(palette);
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("Device", config.device, sizeof(config.device));
  ImGui::SetNextItemWidth(180.0f);
  ImGui::InputScalar("Baud", ImGuiDataType_U32, &config.baudRate);
  PhotonUi::popInputStyle();
}

void drawPcanFields(PCANConfig& config, const PhotonUi::Palette& palette) {
  PhotonUi::pushInputStyle(palette);
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("Channel", config.channel, sizeof(config.channel));
  ImGui::SetNextItemWidth(140.0f);
  ImGui::InputScalar("Bitrate", ImGuiDataType_U32, &config.bitrateKbps);
  ImGui::SetNextItemWidth(140.0f);
  ImGui::InputFloat("Sample", &config.samplePointPercent);
  ImGui::Checkbox("Listen only", &config.listenOnly);
  ImGui::SameLine();
  ImGui::Checkbox("Bus reset", &config.busoffReset);
  PhotonUi::popInputStyle();
}

void drawLogPanel(std::string& log, const PhotonUi::Palette& palette) {
  if (PhotonUi::beginPanel("##NetworkLog", {-1.0f, -1.0f}, palette)) {
    PhotonUi::label("Output", palette);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, PhotonUi::withAlpha(palette.bg, 0.32f));
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

void GUI::connectDaqServer() {
  if (!network) return;
  setConnectionPending(titleBar, "DAQ Server");
  submit(network, kDaqConfig);
}

void GUI::updateNetworkStatus() {
  if (network) drainNetworkLog(network, networkLog, titleBar);
}

void GUI::networkPage(ImGuiWindowFlags flags) {
  static int selected = 0;
  static TCPConfig tcpConfig{};
  static UDPConfig udpConfig{};
  static UARTConfig uartConfig{};
  static PCANConfig pcanConfig{};
  static BLEConfig bleConfig{};
  static WLANConfig wlanConfig{};

  PhotonUi::pushContentStyle();
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
        if (PhotonUi::button("ConnectDaq", "Connect", {104.0f, 38.0f}, palette, true))
          connectDaqServer();
        ImGui::SameLine(0.0f, 8.0f);
        if (PhotonUi::button("DisconnectDaq", "Disconnect", {118.0f, 38.0f}, palette)) {
          setConnectionOffline(titleBar);
          disconnect(network);
        }
      } else if (selected == 1) {
        drawTcpFields(tcpConfig, palette);
        if (PhotonUi::button("ApplyTcp", "Apply", {96.0f, 38.0f}, palette, true)) {
          setConnectionPending(titleBar, "TCP");
          submit(network, tcpConfig);
        }
      } else if (selected == 2) {
        drawUdpFields(udpConfig, palette);
        if (PhotonUi::button("ApplyUdp", "Apply", {96.0f, 38.0f}, palette, true))
          submit(network, udpConfig);
      } else if (selected == 3) {
        drawUartFields(uartConfig, palette);
        if (PhotonUi::button("ApplyUart", "Apply", {96.0f, 38.0f}, palette, true))
          submit(network, uartConfig);
      } else if (selected == 4) {
        drawPcanFields(pcanConfig, palette);
        if (PhotonUi::button("ApplyPcan", "Apply", {96.0f, 38.0f}, palette, true))
          submit(network, pcanConfig);
      } else if (selected == 5) {
        if (PhotonUi::button("ApplyBle", "Apply", {96.0f, 38.0f}, palette, true))
          submit(network, bleConfig);
      } else if (selected == 6) {
        if (PhotonUi::button("ApplyWlan", "Apply", {96.0f, 38.0f}, palette, true))
          submit(network, wlanConfig);
      }
    }
    PhotonUi::endPanel();

    ImGui::Dummy({0.0f, gap});
    drawLogPanel(networkLog, palette);
    ImGui::EndGroup();
  }
  ImGui::End();
  PhotonUi::popContentStyle();
}
