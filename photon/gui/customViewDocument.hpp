#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "customViewTypes.hpp"

struct CustomViewDocumentState {
  std::vector<CustomViewDefinition> panels{};
  int activePanel = 0;
  int nextWidgetPlotId = 10000;
  int nextPanelId = 1;
};

struct CustomViewDocument {
  static CustomViewDocumentState parse(std::string_view text);
  static std::string serialize(const std::vector<CustomViewDefinition>& panels, int activePanel);
};
