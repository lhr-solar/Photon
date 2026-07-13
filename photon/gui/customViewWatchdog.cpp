#include "customViewWatchdog.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "customViewTelemetry.hpp"
#include "imgui.h"
#include "uiComponents.hpp"

void CustomViewWatchdogWidget::draw(Arena* arena, CustomViewWidget& widget) {
  CustomViewWatchdog& dog = widget.watchdog;
  if (arena) {
    CustomViewTelemetry::resolve(*arena, dog);
    CustomViewTelemetry::update(*arena, dog);
  }

  const PhotonUi::Palette palette = PhotonUi::palette();
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = std::max(48.0f, ImGui::GetContentRegionAvail().y);
  ImDrawList* draw = ImGui::GetWindowDrawList();

  if (!dog.source.assigned) {
    draw->AddRectFilled(pos, {pos.x + width, pos.y + height},
                        PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.7f)), 6.0f);
    ImGui::SetCursorScreenPos({pos.x + 10.0f, pos.y + 14.0f});
    ImGui::TextDisabled("Watchdog has no assigned signal.");
  } else if (!dog.hasValue) {
    draw->AddRectFilled(pos, {pos.x + width, pos.y + height},
                        PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.7f)), 6.0f);
    ImGui::SetCursorScreenPos({pos.x + 10.0f, pos.y + 14.0f});
    ImGui::TextDisabled("Waiting for signal data...");
  } else if (dog.tripped) {
    draw->AddRectFilled(pos, {pos.x + width, pos.y + height},
                        PhotonUi::colorU32(PhotonUi::withAlpha(palette.active, 0.85f)), 6.0f);
    draw->AddRect(pos, {pos.x + width, pos.y + height}, PhotonUi::colorU32(palette.accent), 6.0f, 0,
                  2.0f);
    ImGui::SetCursorScreenPos({pos.x + 12.0f, pos.y + 10.0f});
    ImGui::PushTextWrapPos(pos.x + width - 12.0f);
    ImGui::TextUnformatted(dog.message.c_str());
    char valueLine[128]{};
    std::snprintf(valueLine, sizeof(valueLine), "%.3f %s  (threshold %.3f)", dog.latest,
                  dog.unit.c_str(), dog.threshold);
    ImGui::TextUnformatted(valueLine);
    ImGui::PopTextWrapPos();
  } else if (dog.hideWhenOk) {
    draw->AddRectFilled(pos, {pos.x + width, pos.y + height},
                        PhotonUi::colorU32(PhotonUi::withAlpha(palette.raised, 0.45f)), 6.0f);
    ImGui::SetCursorScreenPos({pos.x + 10.0f, pos.y + 14.0f});
    ImGui::TextDisabled("OK  ·  %.3f %s", dog.latest, dog.unit.c_str());
  } else {
    draw->AddRectFilled(pos, {pos.x + width, pos.y + height},
                        PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.75f)), 6.0f);
    ImGui::SetCursorScreenPos({pos.x + 10.0f, pos.y + 14.0f});
    ImGui::Text("OK  ·  %.3f %s", dog.latest, dog.unit.c_str());
  }
  ImGui::SetCursorScreenPos(pos);
  ImGui::Dummy({width, height});
}
