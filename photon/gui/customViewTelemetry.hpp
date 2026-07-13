#pragma once

#include "../parse/arena.hpp"
#include "customViewTypes.hpp"

struct CustomViewTelemetry {
  static void resolve(Arena& arena, PlotManager::PlotSourceRef& source);
  static void resolve(Arena& arena, CustomViewCellGrid& grid);
  static void update(Arena& arena, CustomViewCellGrid& grid);
  static void resolve(Arena& arena, CustomViewWatchdog& watchdog);
  static void update(Arena& arena, CustomViewWatchdog& watchdog);
};
