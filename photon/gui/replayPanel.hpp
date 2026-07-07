#pragma once
#include "../parse/arena.hpp"
#include "../io/replay_controller.hpp"
#include "../io/pre_fault_recorder.hpp"

struct Network;
struct SDL_Window;

namespace gui {
// recorder may be nullptr — if so, recording section is hidden.
void drawReplayPanel(io::Replay_Controller& rc, Arena& arena, Network* network,
                     SDL_Window* window,
                     io::Pre_Fault_Recorder* recorder = nullptr);
} // namespace gui
