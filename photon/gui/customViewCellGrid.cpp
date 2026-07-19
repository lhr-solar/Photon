#include "customViewCellGrid.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "customViewTelemetry.hpp"
#include "imgui.h"
#include "uiComponents.hpp"

namespace {
ImVec4 lerp4(ImVec4 a, ImVec4 b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t);
}

ImVec4 voltageRamp(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  // deep teal -> electric cyan -> lime -> amber -> hot magenta
  if (t < 0.25f) return lerp4({0.05f, 0.28f, 0.42f, 1.0f}, {0.08f, 0.78f, 0.92f, 1.0f}, t / 0.25f);
  if (t < 0.50f)
    return lerp4({0.08f, 0.78f, 0.92f, 1.0f}, {0.35f, 0.95f, 0.35f, 1.0f}, (t - 0.25f) / 0.25f);
  if (t < 0.75f)
    return lerp4({0.35f, 0.95f, 0.35f, 1.0f}, {1.0f, 0.78f, 0.12f, 1.0f}, (t - 0.50f) / 0.25f);
  return lerp4({1.0f, 0.78f, 0.12f, 1.0f}, {1.0f, 0.28f, 0.55f, 1.0f}, (t - 0.75f) / 0.25f);
}

ImVec4 temperatureRamp(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  // ice blue -> violet -> orange -> crimson
  if (t < 0.33f) return lerp4({0.25f, 0.45f, 1.0f, 1.0f}, {0.72f, 0.28f, 1.0f, 1.0f}, t / 0.33f);
  if (t < 0.66f)
    return lerp4({0.72f, 0.28f, 1.0f, 1.0f}, {1.0f, 0.55f, 0.12f, 1.0f}, (t - 0.33f) / 0.33f);
  return lerp4({1.0f, 0.55f, 0.12f, 1.0f}, {1.0f, 0.12f, 0.22f, 1.0f}, (t - 0.66f) / 0.34f);
}

void drawPill(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 fill, ImU32 border, ImU32 text,
              const char* label) {
  const float rounding = (max.y - min.y) * 0.5f;
  draw->AddRectFilled(min, max, fill, rounding);
  draw->AddRect(min, max, border, rounding, 0, 1.2f);
  const ImVec2 size = ImGui::CalcTextSize(label);
  draw->AddText({min.x + (max.x - min.x - size.x) * 0.5f, min.y + (max.y - min.y - size.y) * 0.5f},
                text, label);
}

void drawCellTile(ImDrawList* draw, ImVec2 min, ImVec2 max, ImVec4 base, bool hasValue,
                  bool faulted, bool showVoltage, const char* address, double value) {
  const ImVec4 empty = {0.105f, 0.12f, 0.15f, 1.0f};
  ImVec4 fill = hasValue ? lerp4(base, {0.08f, 0.10f, 0.13f, 1.0f}, 0.72f) : empty;
  if (faulted) fill = {0.42f, 0.10f, 0.13f, 1.0f};
  draw->AddRectFilled(min, max, PhotonUi::colorU32(fill));
  draw->AddRect(min, max, PhotonUi::colorU32(faulted ? ImVec4{1.0f, 0.28f, 0.32f, 1.0f}
                                                       : ImVec4{0.32f, 0.36f, 0.42f, 1.0f}));

  char valueBuf[32]{};
  if (hasValue) {
    if (showVoltage)
      std::snprintf(valueBuf, sizeof(valueBuf), "%.3f V", value);
    else
      std::snprintf(valueBuf, sizeof(valueBuf), "%.1f C", value);
  } else {
    std::snprintf(valueBuf, sizeof(valueBuf), "--");
  }

  draw->AddText({min.x + 5.0f, min.y + 4.0f}, PhotonUi::colorU32({0.62f, 0.68f, 0.75f, 1.0f}),
                address);
  const ImVec2 valueSize = ImGui::CalcTextSize(valueBuf);
  const ImVec2 valuePos{min.x + (max.x - min.x - valueSize.x) * 0.5f,
                        min.y + (max.y - min.y - valueSize.y) * 0.60f};
  draw->AddText(valuePos, PhotonUi::colorU32(hasValue ? ImVec4{0.96f, 0.98f, 1.0f, 1.0f}
                                                       : ImVec4{0.55f, 0.60f, 0.67f, 1.0f}),
                valueBuf);
}

void spreadsheetColumnName(int column, char (&buffer)[8]) {
  int cursor = 0;
  do {
    buffer[cursor++] = static_cast<char>('A' + (column % 26));
    column = column / 26 - 1;
  } while (column >= 0 && cursor < static_cast<int>(sizeof(buffer) - 1));
  std::reverse(buffer, buffer + cursor);
  buffer[cursor] = '\0';
}
}  // namespace

void CustomViewCellGridWidget::draw(Arena* arena, CustomViewWidget& widget) {
  CustomViewCellGrid& grid = widget.cellGrid;
  if (arena) CustomViewTelemetry::update(*arena, grid);

  const PhotonUi::Palette palette = PhotonUi::palette();
  const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 5.5f);

  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 headerOrigin = ImGui::GetCursorScreenPos();
  const float headerH = 34.0f;
  const float availX = std::max(120.0f, ImGui::GetContentRegionAvail().x);
  draw->AddRectFilled(headerOrigin, {headerOrigin.x + availX, headerOrigin.y + headerH},
                      PhotonUi::colorU32(PhotonUi::withAlpha(palette.raised, 0.92f)), 8.0f);
  draw->AddRectFilledMultiColor(headerOrigin, {headerOrigin.x + availX, headerOrigin.y + headerH},
                                PhotonUi::colorU32({0.15f, 0.55f, 0.95f, 0.18f}),
                                PhotonUi::colorU32({0.95f, 0.25f, 0.65f, 0.14f}),
                                PhotonUi::colorU32({0.95f, 0.25f, 0.65f, 0.04f}),
                                PhotonUi::colorU32({0.15f, 0.55f, 0.95f, 0.04f}));

  // Mode pills
  const float pillW = 92.0f;
  const float pillH = 22.0f;
  const float pillY = headerOrigin.y + (headerH - pillH) * 0.5f;
  ImVec2 voltMin{headerOrigin.x + 10.0f, pillY};
  ImVec2 voltMax{voltMin.x + pillW, pillY + pillH};
  ImVec2 tempMin{voltMax.x + 8.0f, pillY};
  ImVec2 tempMax{tempMin.x + pillW, pillY + pillH};

  ImGui::SetCursorScreenPos(voltMin);
  if (ImGui::InvisibleButton("##mode_voltage", {pillW, pillH}))
    grid.mode = CustomViewCellGridMode::Voltage;
  ImGui::SetCursorScreenPos(tempMin);
  if (ImGui::InvisibleButton("##mode_temperature", {pillW, pillH}))
    grid.mode = CustomViewCellGridMode::Temperature;

  const bool voltageMode = grid.mode == CustomViewCellGridMode::Voltage;
  drawPill(draw, voltMin, voltMax,
           PhotonUi::colorU32(voltageMode ? ImVec4{0.10f, 0.72f, 0.95f, 0.95f}
                                          : ImVec4{0.18f, 0.20f, 0.24f, 0.9f}),
           PhotonUi::colorU32(voltageMode ? ImVec4{0.65f, 0.95f, 1.0f, 1.0f}
                                          : PhotonUi::withAlpha(palette.border, 0.7f)),
           PhotonUi::colorU32(voltageMode ? ImVec4{0.02f, 0.08f, 0.12f, 1.0f} : palette.muted),
           "Voltage");
  drawPill(draw, tempMin, tempMax,
           PhotonUi::colorU32(!voltageMode ? ImVec4{1.0f, 0.42f, 0.28f, 0.95f}
                                           : ImVec4{0.18f, 0.20f, 0.24f, 0.9f}),
           PhotonUi::colorU32(!voltageMode ? ImVec4{1.0f, 0.75f, 0.45f, 1.0f}
                                           : PhotonUi::withAlpha(palette.border, 0.7f)),
           PhotonUi::colorU32(!voltageMode ? ImVec4{0.12f, 0.04f, 0.02f, 1.0f} : palette.muted),
           "Temperature");

  char packV[32] = "-- V";
  char packT[32] = "-- C";
  char faultText[32] = "--";
  if (grid.hasPackVoltage) std::snprintf(packV, sizeof(packV), "%.2f V", grid.packVoltage);
  if (grid.hasPackAvgTemp) std::snprintf(packT, sizeof(packT), "%.1f C", grid.packAvgTemp);
  if (grid.hasPackFault)
    std::snprintf(faultText, sizeof(faultText), "%d", static_cast<int>(grid.packFault));

  char statusBuf[128]{};
  if (grid.hasPackVoltage || grid.hasPackAvgTemp || grid.hasPackFault)
    std::snprintf(statusBuf, sizeof(statusBuf), "PACK  %s   %s   FAULT %s", packV, packT,
                  faultText);
  else
    std::snprintf(statusBuf, sizeof(statusBuf),
                  grid.resolved ? "Waiting for BPS frames..." : "Unresolved BPS messages");

  const bool hasFault = grid.hasPackFault && grid.packFault != 0.0;
  const ImVec2 statusSize = ImGui::CalcTextSize(statusBuf);
  const float statusX = headerOrigin.x + availX - statusSize.x - 14.0f;
  draw->AddText({statusX, headerOrigin.y + (headerH - statusSize.y) * 0.5f},
                PhotonUi::colorU32(hasFault ? ImVec4{1.0f, 0.35f + 0.25f * pulse, 0.35f, 1.0f}
                                            : ImVec4{0.85f, 0.92f, 1.0f, 0.95f}),
                statusBuf);

  ImGui::SetCursorScreenPos({headerOrigin.x, headerOrigin.y + headerH + 8.0f});

  // Legend strip
  const ImVec2 legendOrigin = ImGui::GetCursorScreenPos();
  const float legendH = 10.0f;
  const float legendW = availX;
  for (int i = 0; i < 64; ++i) {
    const float t0 = static_cast<float>(i) / 64.0f;
    const float t1 = static_cast<float>(i + 1) / 64.0f;
    const ImVec4 c = voltageMode ? voltageRamp(t0) : temperatureRamp(t0);
    draw->AddRectFilled({legendOrigin.x + legendW * t0, legendOrigin.y},
                        {legendOrigin.x + legendW * t1, legendOrigin.y + legendH},
                        PhotonUi::colorU32(c));
  }
  draw->AddRect(legendOrigin, {legendOrigin.x + legendW, legendOrigin.y + legendH},
                PhotonUi::colorU32(PhotonUi::withAlpha(palette.border, 0.8f)), 2.0f);
  ImGui::Dummy({availX, legendH + 8.0f});

  const ImVec2 avail = ImGui::GetContentRegionAvail();
  constexpr float rowHeaderW = 28.0f;
  constexpr float columnHeaderH = 22.0f;
  const float cellW = std::max(8.0f, (avail.x - rowHeaderW) / static_cast<float>(grid.cols));
  const float cellH = std::max(8.0f, (avail.y - columnHeaderH) / static_cast<float>(grid.rows));
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImU32 headerFill = PhotonUi::colorU32({0.19f, 0.22f, 0.27f, 1.0f});
  const ImU32 headerBorder = PhotonUi::colorU32({0.36f, 0.40f, 0.46f, 1.0f});
  const ImU32 headerText = PhotonUi::colorU32({0.82f, 0.86f, 0.92f, 1.0f});
  draw->AddRectFilled(origin, {origin.x + rowHeaderW, origin.y + columnHeaderH}, headerFill);
  draw->AddRect(origin, {origin.x + rowHeaderW, origin.y + columnHeaderH}, headerBorder);
  for (int col = 0; col < grid.cols; ++col) {
    const ImVec2 min{origin.x + rowHeaderW + col * cellW, origin.y};
    const ImVec2 max{min.x + cellW, min.y + columnHeaderH};
    char label[8]{};
    spreadsheetColumnName(col, label);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    draw->AddRectFilled(min, max, headerFill);
    draw->AddRect(min, max, headerBorder);
    draw->AddText({min.x + (cellW - textSize.x) * 0.5f, min.y + (columnHeaderH - textSize.y) * 0.5f},
                  headerText, label);
  }

  constexpr float kVoltageMin = 3.0f;
  constexpr float kVoltageMax = 4.2f;
  constexpr float kTempMin = 15.0f;
  constexpr float kTempMax = 60.0f;

  const int cellCount = std::min(kCellGridCapacity, grid.cols * grid.rows);
  for (int row = 0; row < grid.rows; ++row) {
    const ImVec2 min{origin.x, origin.y + columnHeaderH + row * cellH};
    const ImVec2 max{min.x + rowHeaderW, min.y + cellH};
    char label[8]{};
    std::snprintf(label, sizeof(label), "%d", row + 1);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    draw->AddRectFilled(min, max, headerFill);
    draw->AddRect(min, max, headerBorder);
    draw->AddText({min.x + (rowHeaderW - textSize.x) * 0.5f, min.y + (cellH - textSize.y) * 0.5f},
                  headerText, label);
  }
  for (int index = 0; index < cellCount; ++index) {
    const int row = index / grid.cols;
    const int col = index % grid.cols;
    const ImVec2 min(origin.x + rowHeaderW + static_cast<float>(col) * cellW,
                     origin.y + columnHeaderH + static_cast<float>(row) * cellH);
    const ImVec2 max(min.x + cellW, min.y + cellH);
    const CustomViewCellSample& cell = grid.cells[static_cast<size_t>(index)];
    const bool hasValue = voltageMode ? cell.hasVoltage : cell.hasTemperature;
    const double value = voltageMode ? cell.voltage : cell.temperature;
    const bool faulted = (cell.hasVoltage && cell.voltageFault != 0.0) ||
                         (cell.hasTemperature && cell.temperatureFault != 0.0);

    float t = 0.0f;
    if (hasValue) {
      if (voltageMode)
        t = static_cast<float>((value - kVoltageMin) / (kVoltageMax - kVoltageMin));
      else
        t = static_cast<float>((value - kTempMin) / (kTempMax - kTempMin));
    }
    const ImVec4 base = voltageMode ? voltageRamp(t) : temperatureRamp(t);
    char address[16]{};
    // The array is keyed directly by BPS_Tap_idx, so display that real tap ID.
    std::snprintf(address, sizeof(address), "ID %02d", index);
    drawCellTile(draw, min, max, base, hasValue, faulted, voltageMode, address, value);
  }

  ImGui::Dummy(ImVec2(avail.x, columnHeaderH + static_cast<float>(grid.rows) * cellH));
}
