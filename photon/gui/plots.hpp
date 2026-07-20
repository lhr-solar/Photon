#pragma once

#include <cstdint>

#include "../parse/arena.hpp"
#include "imgui.h"
#include "implot.h"

struct Network;

enum class TimelineMode : uint8_t { Paused, Buffering, Playing, Live, Unavailable };

struct Plots {
  static bool signalStatic(Arena& arena, uint32_t id, uint32_t signal, ImVec2 size,
                           const ImPlotSpec& spec = ImPlotSpec());
  bool signal(Arena& arena, uint32_t id, uint32_t signal, ImVec2 size,
              const ImPlotSpec& spec = ImPlotSpec());
  void timeline(Arena& arena, Network* network, bool serverConnected, ImVec2 pos, ImVec2 size);
  double cursor{};

 private:
  struct CursorIndex {
    uint64_t generation = -1;
    double time{};
    double window{};
    uint32_t id = MESSAGE_MAX;
    uint32_t count{};
    uint32_t first{};
    uint32_t last{};
  } index;
  TimelineMode timelineMode = TimelineMode::Live;
  double playTarget{};
  double bufferingSince{};
  uint64_t playbackStatusSequence{};
  double windowSeconds = 2.0;
  int calendarYear{};
  int calendarMonth{};
  uint8_t timelineLevel{};
  uint8_t timelineDragging{};
};
