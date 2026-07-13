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
                  bool faulted, bool showVoltage, int index, double value, float pulse) {
  const float rounding = 7.0f;
  const ImVec4 empty = {0.12f, 0.14f, 0.18f, 0.85f};
  ImVec4 fill = hasValue ? base : empty;
  if (hasValue) {
    fill.x = std::min(1.0f, fill.x * 1.15f + 0.05f);
    fill.y = std::min(1.0f, fill.y * 1.10f + 0.04f);
    fill.z = std::min(1.0f, fill.z * 1.10f + 0.04f);
  }

  // Soft outer glow for live cells.
  if (hasValue) {
    ImVec4 glow = fill;
    glow.w = 0.22f + 0.10f * pulse;
    const float inflate = 2.5f + pulse * 1.5f;
    draw->AddRectFilled({min.x - inflate, min.y - inflate}, {max.x + inflate, max.y + inflate},
                        PhotonUi::colorU32(glow), rounding + 2.0f);
  }

  const ImVec4 top = hasValue ? lerp4(fill, {1.0f, 1.0f, 1.0f, 1.0f}, 0.22f) : empty;
  draw->AddRectFilled(min, max, PhotonUi::colorU32(fill), rounding);
  if (hasValue) {
    const float midY = min.y + (max.y - min.y) * 0.55f;
    draw->AddRectFilled(min, {max.x, midY}, PhotonUi::colorU32(PhotonUi::withAlpha(top, 0.55f)),
                        rounding);
  }

  ImVec4 borderCol =
      hasValue ? lerp4(fill, {1.0f, 1.0f, 1.0f, 1.0f}, 0.45f) : ImVec4{0.28f, 0.32f, 0.38f, 0.9f};
  borderCol.w = 0.95f;
  float borderThickness = 1.4f;
  if (faulted) {
    borderCol = {1.0f, 0.20f + 0.25f * pulse, 0.28f, 1.0f};
    borderThickness = 2.4f + pulse;
  }
  draw->AddRect(min, max, PhotonUi::colorU32(borderCol), rounding, 0, borderThickness);

  // Specular strip.
  if (hasValue) {
    const float stripH = std::max(3.0f, (max.y - min.y) * 0.18f);
    draw->AddRectFilled({min.x + 3.0f, min.y + 3.0f}, {max.x - 3.0f, min.y + 3.0f + stripH},
                        PhotonUi::colorU32({1.0f, 1.0f, 1.0f, 0.16f}), 3.0f);
  }

  char indexBuf[16]{};
  char valueBuf[32]{};
  std::snprintf(indexBuf, sizeof(indexBuf), "#%02d", index);
  if (hasValue) {
    if (showVoltage)
      std::snprintf(valueBuf, sizeof(valueBuf), "%.3f V", value);
    else
      std::snprintf(valueBuf, sizeof(valueBuf), "%.1f C", value);
  } else {
    std::snprintf(valueBuf, sizeof(valueBuf), "--");
  }

  const float pad = 5.0f;
  draw->AddText({min.x + pad, min.y + pad},
                PhotonUi::colorU32(hasValue ? ImVec4{1.0f, 1.0f, 1.0f, 0.72f}
                                            : ImVec4{0.55f, 0.58f, 0.64f, 0.85f}),
                indexBuf);

  const ImVec2 valueSize = ImGui::CalcTextSize(valueBuf);
  const ImVec2 valuePos{min.x + (max.x - min.x - valueSize.x) * 0.5f,
                        min.y + (max.y - min.y - valueSize.y) * 0.55f};
  // Soft text shadow for punch.
  draw->AddText({valuePos.x + 1.0f, valuePos.y + 1.0f},
                PhotonUi::colorU32({0.0f, 0.0f, 0.0f, 0.55f}), valueBuf);
  draw->AddText(valuePos,
                PhotonUi::colorU32(hasValue ? ImVec4{1.0f, 1.0f, 1.0f, 1.0f}
                                            : ImVec4{0.62f, 0.66f, 0.72f, 0.9f}),
                valueBuf);
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
  const float gap = 6.0f;
  const float cellW = std::max(
      8.0f, (avail.x - gap * static_cast<float>(grid.cols - 1)) / static_cast<float>(grid.cols));
  const float cellH = std::max(
      8.0f, (avail.y - gap * static_cast<float>(grid.rows - 1)) / static_cast<float>(grid.rows));
  const ImVec2 origin = ImGui::GetCursorScreenPos();

  constexpr float kVoltageMin = 3.0f;
  constexpr float kVoltageMax = 4.2f;
  constexpr float kTempMin = 15.0f;
  constexpr float kTempMax = 60.0f;

  const int cellCount = std::min(kCellGridCapacity, grid.cols * grid.rows);
  for (int index = 0; index < cellCount; ++index) {
    const int row = index / grid.cols;
    const int col = index % grid.cols;
    const ImVec2 min(origin.x + static_cast<float>(col) * (cellW + gap),
                     origin.y + static_cast<float>(row) * (cellH + gap));
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
    drawCellTile(draw, min, max, base, hasValue, faulted, voltageMode, index, value, pulse);
  }

  ImGui::Dummy(ImVec2(avail.x, static_cast<float>(grid.rows) * (cellH + gap) - gap));
}
