#pragma once

#include <cstdint>

#include "../parse/arena.hpp"
#include "imgui.h"
#include "implot.h"

struct Plots {
  static bool signal(Arena& arena, uint32_t id, uint32_t signal, ImVec2 size,
                     const ImPlotSpec& spec = ImPlotSpec());
};
