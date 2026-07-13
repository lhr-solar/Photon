#pragma once

#include "../parse/arena.hpp"
#include "plots.hpp"

struct PlotRenderer {
  static void render(Arena* arena, PlotManager::PlotWindow& plot);
};
