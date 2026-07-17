#pragma once

#include "../parse/arena.hpp"
#include "plots.hpp"

struct Network;

struct PlotRenderer {
  static void render(Arena* arena, Network* network, PlotManager::PlotWindow& plot);
};
