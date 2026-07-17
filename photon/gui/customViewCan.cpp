#include "customViewCan.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../network/network.hpp"
#include "../parse/arena.hpp"
#include "customViewTypes.hpp"
#include "imgui.h"
#include "uiComponents.hpp"

namespace {

struct MonitorRow {
  CANFrameEvent frame{};
  uint64_t count = 0;
  double lastUiTime = 0.0;
  double periodMs = 0.0;
};

struct MonitorRuntime {
  Network* network = nullptr;
  SPMCQueue<CANFrameEvent, 512>::Reader reader{};
  std::unordered_map<uint32_t, MonitorRow> rows{};
  std::ofstream record{};
  std::string recordingWidget{};
  uint64_t recordingStartedAt = 0;
};

MonitorRuntime& monitorRuntime() {
  static MonitorRuntime runtime{};
  return runtime;
}

Message* messageFor(Arena* arena, uint32_t id) {
  if (!arena || id >= arena->messages.size()) return nullptr;
  return arena->messages[id];
}

Message* messageByName(Arena* arena, const char* name) {
  if (!arena) return nullptr;
  for (uint32_t id : arena->validIds) {
    Message* message = messageFor(arena, id);
    if (message && message->name == name) return message;
  }
  return nullptr;
}

Signal* signalByName(Message* message, const char* name) {
  if (!message) return nullptr;
  for (uint32_t index = 0; index < message->signalCount; ++index) {
    Signal* signal = message->signals[index];
    if (signal && signal->name == name) return signal;
  }
  return nullptr;
}

const SignalValueDescription* stateForValue(const Signal& signal, double physicalValue) {
  if (signal.scale == 0.0) return nullptr;
  const int64_t rawValue =
      static_cast<int64_t>(std::llround((physicalValue - signal.offset) / signal.scale));
  for (const auto& description : signal.valueDescriptions)
    if (description.rawValue == rawValue) return &description;
  return nullptr;
}

std::string displayValue(const Signal& signal, double value) {
  if (const SignalValueDescription* state = stateForValue(signal, value)) return state->label;
  char text[48]{};
  std::snprintf(text, sizeof(text), "%.6g", value);
  return text;
}

std::string signalPreview(Arena* arena, uint32_t id, size_t maxSignals = 3) {
  Message* message = messageFor(arena, id);
  if (!message) return "No DBC match";
  std::string text;
  for (uint32_t i = 0; i < message->signalCount && maxSignals > 0; ++i) {
    Signal* signal = message->signals[i];
    if (!signal) continue;
    void* data = nullptr;
    uint32_t bytes = 0;
    arena->read(id, i, &data, &bytes);
    if (!data || bytes < sizeof(double)) continue;
    const auto* values = static_cast<const double*>(data);
    const double value = values[bytes / sizeof(double) - 1];
    if (!text.empty()) text += "  ";
    text += signal->name + "=";
    text += displayValue(*signal, value);
    if (!signal->unit.empty() && signal->unit != "NULL") text += " " + signal->unit;
    --maxSignals;
  }
  return text.empty() ? "Waiting for decoded values" : text;
}

void writeCapture(std::ofstream& stream, uint64_t start, const CANFrameEvent& frame) {
  if (!stream || start == 0) return;
  const double seconds = static_cast<double>(frame.timestampMs - start) / 1000.0;
  char id[16]{};
  std::snprintf(id, sizeof(id), frame.id > 0x7FF ? "%08X" : "%03X", frame.id);
  stream << "(";
  stream.setf(std::ios::fixed);
  stream.precision(6);
  stream << seconds << ") can0 " << id << "#";
  for (uint8_t i = 0; i < frame.dlc; ++i) {
    char byte[3]{};
    std::snprintf(byte, sizeof(byte), "%02X", frame.data[i]);
    stream << byte;
  }
  stream << "\n";
}

void drainMonitor(Network* network) {
  MonitorRuntime& runtime = monitorRuntime();
  if (!network) return;
  if (runtime.network != network) {
    runtime.network = network;
    runtime.reader = network->canFrameBuffer.getReader();
    runtime.rows.clear();
    runtime.record.close();
    runtime.recordingWidget.clear();
    runtime.recordingStartedAt = 0;
  }
  while (CANFrameEvent* event = runtime.reader.read()) {
    MonitorRow& row = runtime.rows[event->id];
    if (row.count > 0 && event->timestampMs >= row.frame.timestampMs) {
      const double nextPeriod = static_cast<double>(event->timestampMs - row.frame.timestampMs);
      row.periodMs = row.periodMs == 0.0 ? nextPeriod : row.periodMs * 0.8 + nextPeriod * 0.2;
    }
    row.frame = *event;
    ++row.count;
    row.lastUiTime = ImGui::GetTime();
    if (runtime.record) {
      if (runtime.recordingStartedAt == 0) runtime.recordingStartedAt = event->timestampMs;
      writeCapture(runtime.record, runtime.recordingStartedAt, *event);
      runtime.record.flush();
    }
  }
}

bool matchesFilter(const MonitorRow& row, Arena* arena, const std::string& filter) {
  if (filter.empty()) return true;
  std::string text = signalPreview(arena, row.frame.id, 32);
  if (Message* message = messageFor(arena, row.frame.id)) text += " " + message->name;
  char id[16]{};
  std::snprintf(id, sizeof(id), "%X", row.frame.id);
  text += " ";
  text += id;
  std::string lowerFilter = filter;
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
  std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return text.find(lowerFilter) != std::string::npos;
}

bool hasMessage(Arena* arena, const char* name) {
  if (!arena) return false;
  for (uint32_t id : arena->validIds) {
    Message* message = messageFor(arena, id);
    if (message && message->name == name) return true;
  }
  return false;
}

struct ScheduledControl {
  bool enabled = false;
  double intervalMs = 100.0;
  double lastSent = -1.0;
};

struct ControlsRuntime {
  std::unordered_map<std::string, ScheduledControl> schedules{};
  int ignition = 0;
  int gear = 1;
  double accel = 0.0;
  double brake = 0.0;
  double pressure1 = 0.0;
  double pressure2 = 0.0;
  double steering = 0.0;
  int64_t bpsFaultRaw = 0;
  bool bpsContactors = false;
  bool mcFault = false;
  double vehicleVelocity = 0.0;
  double motorVelocity = 0.0;
  uint32_t pedalCounter = 0;
  uint32_t steeringCounter = 0;
  uint32_t timestamp = 0;
};

std::unordered_map<std::string, ControlsRuntime>& controlsRuntimes() {
  static std::unordered_map<std::string, ControlsRuntime> runtimes{};
  return runtimes;
}

bool sendWhenDue(Network* network, ScheduledControl& control, const char* message,
                 std::vector<CANSignalValue> values, double now) {
  if (!network || !network->canControlsArmed() || !control.enabled) return false;
  if (control.lastSent >= 0.0 && now - control.lastSent < control.intervalMs / 1000.0) return false;
  control.lastSent = now;
  return network->sendDBCFrame(message, std::move(values), true);
}

void drawScheduleHeader(const char* label, ScheduledControl& control, bool available) {
  ImGui::Checkbox((std::string("Send ") + label).c_str(), &control.enabled);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(92.0f);
  ImGui::InputDouble((std::string("##rate") + label).c_str(), &control.intervalMs, 5.0, 25.0,
                     "%.0f ms");
  control.intervalMs = std::clamp(control.intervalMs, 10.0, 5000.0);
  if (!available) {
    ImGui::SameLine();
    ImGui::TextDisabled("DBC message unavailable");
    control.enabled = false;
  }
}

bool drawDBCStateCombo(const char* label, Signal* signal, int64_t& rawValue) {
  if (!signal || signal->valueDescriptions.empty()) {
    ImGui::TextDisabled("%s: unavailable in active DBC", label);
    return false;
  }
  const SignalValueDescription* selected = nullptr;
  for (const auto& description : signal->valueDescriptions)
    if (description.rawValue == rawValue) selected = &description;
  const char* preview = selected ? selected->label.c_str() : "Select state";
  float width = ImGui::CalcTextSize(preview).x;
  for (const auto& description : signal->valueDescriptions)
    width = std::max(width, ImGui::CalcTextSize(description.label.c_str()).x);
  width = std::clamp(width + ImGui::GetStyle().FramePadding.x * 4.0f + 18.0f, 120.0f, 280.0f);
  ImGui::TextUnformatted(label);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(width);
  const std::string id = std::string("##") + label;
  if (!ImGui::BeginCombo(id.c_str(), preview)) return false;
  for (const auto& description : signal->valueDescriptions) {
    const bool isSelected = description.rawValue == rawValue;
    if (ImGui::Selectable(description.label.c_str(), isSelected)) rawValue = description.rawValue;
    if (isSelected) ImGui::SetItemDefaultFocus();
  }
  ImGui::EndCombo();
  return true;
}

float optionWidth(const char* const choices[], int count) {
  float width = 0.0f;
  for (int index = 0; index < count; ++index)
    width = std::max(width, ImGui::CalcTextSize(choices[index]).x);
  return std::clamp(width + ImGui::GetStyle().FramePadding.x * 4.0f + 18.0f, 100.0f, 220.0f);
}

void drawCompactCombo(const char* label, const char* id, int* selected, const char* const choices[],
                      int count) {
  ImGui::TextUnformatted(label);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(optionWidth(choices, count));
  ImGui::Combo(id, selected, choices, count);
}

void drawCompactNumber(const char* label, const char* id, double& value, double speed,
                       const char* format) {
  ImGui::TextUnformatted(label);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(118.0f);
  ImGui::DragScalar(id, ImGuiDataType_Double, &value, speed, nullptr, nullptr, format);
}

}  // namespace

void CustomViewCan::drawMonitor(Arena* arena, Network* network, CustomViewWidget& widget) {
  CustomViewCanMonitor& config = widget.canMonitor;
  drainMonitor(network);
  MonitorRuntime& runtime = monitorRuntime();
  const PhotonUi::Palette palette = PhotonUi::palette();

  std::array<char, 128> filter{};
  std::snprintf(filter.data(), filter.size(), "%s", config.filter.c_str());
  ImGui::SetNextItemWidth(190.0f);
  if (ImGui::InputTextWithHint("##can_filter", "Filter ID, message, signal", filter.data(), filter.size()))
    config.filter = filter.data();
  ImGui::SameLine();
  const char* sorts[] = {"ID", "Name", "Count", "Age"};
  ImGui::SetNextItemWidth(82.0f);
  ImGui::Combo("##can_sort", &config.sort, sorts, IM_ARRAYSIZE(sorts));
  ImGui::SameLine();
  if (ImGui::Button(runtime.record ? "Stop recording" : "Record")) {
    if (runtime.record) {
      runtime.record.close();
      runtime.recordingWidget.clear();
    } else if (!config.recordPath.empty()) {
      const std::filesystem::path path(config.recordPath);
      std::error_code error;
      if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);
      runtime.record.open(config.recordPath, std::ios::out | std::ios::trunc);
      if (runtime.record) {
        runtime.recordingWidget = widget.id;
        runtime.recordingStartedAt = 0;
      }
    }
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1.0f);
  std::array<char, 256> capturePath{};
  std::snprintf(capturePath.data(), capturePath.size(), "%s", config.recordPath.c_str());
  if (ImGui::InputText("##capture_path", capturePath.data(), capturePath.size()) && !runtime.record)
    config.recordPath = capturePath.data();
  if (runtime.record) ImGui::TextColored(palette.accent, "Recording %s", config.recordPath.c_str());

  std::vector<MonitorRow*> rows{};
  rows.reserve(runtime.rows.size());
  for (auto& [id, row] : runtime.rows)
    if (matchesFilter(row, arena, config.filter)) rows.push_back(&row);
  const double now = ImGui::GetTime();
  std::sort(rows.begin(), rows.end(), [&](const MonitorRow* left, const MonitorRow* right) {
    if (config.sort == 1) {
      const Message* a = messageFor(arena, left->frame.id);
      const Message* b = messageFor(arena, right->frame.id);
      return (a ? a->name : "") < (b ? b->name : "");
    }
    if (config.sort == 2) return left->count > right->count;
    if (config.sort == 3) return left->lastUiTime > right->lastUiTime;
    return left->frame.id < right->frame.id;
  });
  // The full-detail mode stays tied to the selected frame.  Up/down moves to
  // adjacent messages while PageUp/PageDown remains available for scrolling a
  // long signal list, matching the bench TUI workflow.
  if (!ImGui::GetIO().WantTextInput && !rows.empty() && config.selectedId != UINT32_MAX) {
    const auto selected = std::find_if(rows.begin(), rows.end(), [&](const MonitorRow* row) {
      return row->frame.id == config.selectedId;
    });
    if (selected != rows.end()) {
      const size_t index = static_cast<size_t>(std::distance(rows.begin(), selected));
      if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false) || ImGui::IsKeyPressed(ImGuiKey_J, false))
        config.selectedId = rows[index == 0 ? 0 : index - 1]->frame.id;
      if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false) || ImGui::IsKeyPressed(ImGuiKey_K, false))
        config.selectedId = rows[std::min(rows.size() - 1, index + 1)]->frame.id;
    }
  }

  if (ImGui::BeginTable("##can_monitor", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                        {0.0f, std::max(90.0f, ImGui::GetContentRegionAvail().y * 0.55f)})) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 56.0f);
    ImGui::TableSetupColumn("Cycle", ImGuiTableColumnFlags_WidthFixed, 62.0f);
    ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Latest values", ImGuiTableColumnFlags_WidthStretch, 2.1f);
    ImGui::TableHeadersRow();
    for (MonitorRow* row : rows) {
      const bool selected = config.selectedId == row->frame.id;
      ImGui::TableNextRow();
      ImGui::PushID(static_cast<int>(row->frame.id));
      ImGui::TableSetColumnIndex(0);
      if (ImGui::Selectable("##select", selected, ImGuiSelectableFlags_SpanAllColumns))
        config.selectedId = row->frame.id;
      ImGui::SameLine();
      ImGui::Text("%03X", row->frame.id);
      ImGui::TableSetColumnIndex(1);
      if (Message* message = messageFor(arena, row->frame.id))
        ImGui::TextUnformatted(message->name.c_str());
      else
        ImGui::TextDisabled("Unknown frame");
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%llu", static_cast<unsigned long long>(row->count));
      ImGui::TableSetColumnIndex(3);
      ImGui::Text(row->periodMs > 0.0 ? "%.0f ms" : "--", row->periodMs);
      ImGui::TableSetColumnIndex(4);
      ImGui::Text("%.1f s", std::max(0.0, now - row->lastUiTime));
      ImGui::TableSetColumnIndex(5);
      ImGui::TextUnformatted(signalPreview(arena, row->frame.id).c_str());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  if (config.selectedId != UINT32_MAX) {
    Message* message = messageFor(arena, config.selectedId);
    ImGui::SeparatorText(message ? message->name.c_str() : "Selected raw frame");
    if (!message) {
      ImGui::TextDisabled("No active DBC definition for 0x%X", config.selectedId);
      return;
    }
    if (ImGui::BeginChild("##can_detail", {0.0f, 0.0f}, ImGuiChildFlags_Borders)) {
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        const float page = ImGui::GetWindowHeight() * 0.75f;
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false))
          ImGui::SetScrollY(ImGui::GetScrollY() + page);
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp, false))
          ImGui::SetScrollY(std::max(0.0f, ImGui::GetScrollY() - page));
      }
      for (uint32_t index = 0; index < message->signalCount; ++index) {
        Signal* signal = message->signals[index];
        if (!signal) continue;
        void* data = nullptr;
        uint32_t bytes = 0;
        arena->read(message->id, index, &data, &bytes);
        if (data && bytes >= sizeof(double)) {
          const auto* values = static_cast<const double*>(data);
          const double value = values[bytes / sizeof(double) - 1];
          const std::string display = displayValue(*signal, value);
          ImGui::Text("%s: %s %s", signal->name.c_str(), display.c_str(),
                      signal->unit == "NULL" ? "" : signal->unit.c_str());
        } else {
          ImGui::TextDisabled("%s: --", signal->name.c_str());
        }
      }
    }
    ImGui::EndChild();
  }
}

void CustomViewCan::drawControls(Arena* arena, Network* network, CustomViewWidget& widget) {
  ControlsRuntime& state = controlsRuntimes()[widget.id];
  const double now = ImGui::GetTime();
  const bool transmitReady = network && network->canTransmitCAN();
  if (!transmitReady) ImGui::TextDisabled("Connect PCAN with Listen only disabled to enable controls.");
  if (network) {
    if (network->canControlsArmed()) {
      if (ImGui::Button("STOP ALL && DISARM", {170.0f, 0.0f})) {
        for (auto& [name, schedule] : state.schedules) schedule.enabled = false;
        network->armCanControls(false);
      }
      ImGui::SameLine();
      ImGui::TextColored(PhotonUi::palette().accent, "CAN CONTROLS ARMED");
    } else {
      if (!transmitReady) ImGui::BeginDisabled();
      if (ImGui::Button("Arm CAN controls", {150.0f, 0.0f})) network->armCanControls(true);
      if (!transmitReady) ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::TextDisabled("Arming is required for every List edit and preset.");
    }
  }

  const char* ignitionChoices[] = {"Off", "Array", "Motor"};
  const char* gearChoices[] = {"Forward", "Neutral", "Reverse"};
  ImGui::SeparatorText("Driver input");
  if (ImGui::BeginTable("##driver_input", 2, ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableNextColumn();
    drawCompactCombo("Ignition", "##ignition", &state.ignition, ignitionChoices,
                     IM_ARRAYSIZE(ignitionChoices));
    ImGui::TableNextColumn();
    drawCompactCombo("Gear", "##gear", &state.gear, gearChoices, IM_ARRAYSIZE(gearChoices));
    ImGui::EndTable();
  }
  ScheduledControl& driver = state.schedules["Driver_Input_Status"];
  drawScheduleHeader("driver input", driver, hasMessage(arena, "Driver_Input_Status"));
  sendWhenDue(network, driver, "Driver_Input_Status",
              {{"Ignition_Array", state.ignition == 1 ? 1.0 : 0.0},
               {"Ignition_Motor", state.ignition == 2 ? 1.0 : 0.0},
               {"Ignition_Off", state.ignition == 0 ? 1.0 : 0.0},
               {"Gear_Forward", state.gear == 0 ? 1.0 : 0.0},
               {"Gear_Neutral", state.gear == 1 ? 1.0 : 0.0},
               {"Gear_Reverse", state.gear == 2 ? 1.0 : 0.0}}, now);

  ImGui::SeparatorText("Pedals and brake pressure");
  if (ImGui::BeginTable("##pedal_values", 2, ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableNextColumn();
    drawCompactNumber("Accelerator (%)", "##accel", state.accel, 1.0, "%.1f");
    ImGui::TableNextColumn();
    drawCompactNumber("Brake (%)", "##brake", state.brake, 1.0, "%.1f");
    ImGui::EndTable();
  }
  state.accel = std::clamp(state.accel, 0.0, 100.0);
  state.brake = std::clamp(state.brake, 0.0, 100.0);
  ScheduledControl& pedal = state.schedules["Pedal_Status"];
  pedal.intervalMs = 50.0;
  drawScheduleHeader("pedals", pedal, hasMessage(arena, "Pedal_Status"));
  const double pedalCounter = static_cast<double>(state.pedalCounter);
  if (sendWhenDue(network, pedal, "Pedal_Status",
                  {{"AccelPedal_Main_Pos", state.accel}, {"AccelPedal_Redundant_Pos", state.accel},
                   {"BrakePedal_Main_Pos", state.brake}, {"BrakePedal_Redundant_Pos", state.brake},
                   {"FrameID_Pedals", pedalCounter}}, now))
    state.pedalCounter = (state.pedalCounter + 1) & 0xFF;
  if (ImGui::BeginTable("##pressure_values", 2, ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableNextColumn();
    drawCompactNumber("Pressure 1 (PSI)", "##pressure1", state.pressure1, 5.0, "%.1f");
    ImGui::TableNextColumn();
    drawCompactNumber("Pressure 2 (PSI)", "##pressure2", state.pressure2, 5.0, "%.1f");
    ImGui::EndTable();
  }
  state.pressure1 = std::clamp(state.pressure1, 0.0, 3000.0);
  state.pressure2 = std::clamp(state.pressure2, 0.0, 3000.0);
  for (const auto& [name, value] : std::array<std::pair<const char*, double>, 2>{{{"Brake_Pressure_1", state.pressure1}, {"Brake_Pressure_2", state.pressure2}}}) {
    ScheduledControl& pressure = state.schedules[name];
    pressure.intervalMs = 50.0;
    drawScheduleHeader(name, pressure, hasMessage(arena, name));
    const double counter = static_cast<double>(state.pedalCounter);
    if (sendWhenDue(network, pressure, name,
                    {{"Brake_Pressure", value}, {"Brake_Pressure_ADC", 0.0},
                     {"FrameID_Pedals", counter}}, now))
      state.pedalCounter = (state.pedalCounter + 1) & 0xFF;
  }

  ImGui::SeparatorText("BPS, motor controller, and velocity");
  Signal* bpsFault = signalByName(messageByName(arena, "BPS_Status"), "BPS_Fault");
  drawDBCStateCombo("BPS fault", bpsFault, state.bpsFaultRaw);
  ImGui::Checkbox("BPS contactors closed", &state.bpsContactors);
  ImGui::SameLine();
  ImGui::Checkbox("Inject MC hardware fault", &state.mcFault);
  ScheduledControl& bps = state.schedules["BPS_Status"];
  drawScheduleHeader("BPS status", bps, hasMessage(arena, "BPS_Status"));
  sendWhenDue(network, bps, "BPS_Status",
              {{"BPS_Fault", bpsFault ? static_cast<double>(state.bpsFaultRaw) * bpsFault->scale +
                                            bpsFault->offset
                                        : 0.0},
               {"HV_Plus_Contactor_State", state.bpsContactors ? 1.0 : 0.0},
               {"HV_Minus_Contactor_State", state.bpsContactors ? 1.0 : 0.0},
               {"Array_Contactor_State", state.bpsContactors ? 1.0 : 0.0},
               {"Array_Precharge_Contactor_State", state.bpsContactors ? 1.0 : 0.0}}, now);
  ScheduledControl& mc = state.schedules["MC_Status"];
  drawScheduleHeader("motor status", mc, hasMessage(arena, "MC_Status"));
  sendWhenDue(network, mc, "MC_Status", {{"MC_FAULT_HardwareOverCurrent", state.mcFault ? 1.0 : 0.0}}, now);
  if (ImGui::BeginTable("##velocity_values", 2, ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableNextColumn();
    drawCompactNumber("Vehicle velocity (m/s)", "##vehicle_velocity", state.vehicleVelocity, 0.25,
                      "%.2f");
    ImGui::TableNextColumn();
    drawCompactNumber("Motor velocity (rpm)", "##motor_velocity", state.motorVelocity, 25.0,
                      "%.0f");
    ImGui::EndTable();
  }
  ScheduledControl& velocity = state.schedules["MC_VelocityMeasurement"];
  velocity.intervalMs = 200.0;
  drawScheduleHeader("velocity", velocity, hasMessage(arena, "MC_VelocityMeasurement"));
  sendWhenDue(network, velocity, "MC_VelocityMeasurement",
              {{"MC_VehicleVelocity", state.vehicleVelocity}, {"MC_MotorVelocity", state.motorVelocity}}, now);

  ImGui::SeparatorText("Telemetry and steering");
  ScheduledControl& timestamp = state.schedules["TelemLeader_Center"];
  timestamp.intervalMs = 100.0;
  drawScheduleHeader("telemetry timestamp", timestamp, hasMessage(arena, "TelemLeader_Center"));
  if (sendWhenDue(network, timestamp, "TelemLeader_Center",
                  {{"Timestamp", static_cast<double>(state.timestamp)}}, now))
    state.timestamp = (state.timestamp + 1) & 0xFFFF;
  drawCompactNumber("Steering angle (deg)", "##steering", state.steering, 1.0, "%.1f");
  state.steering = std::clamp(state.steering, -780.0, 780.0);
  ImGui::TextDisabled("LWS_Standard TX is intentionally disabled: LWS_CHK_SUM needs the approved checksum algorithm.");
}
