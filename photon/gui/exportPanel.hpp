#pragma once
#include "../parse/arena.hpp"

struct SDL_Window;
namespace io { class Replay_Controller; class Pre_Fault_Recorder; }

namespace gui {
void drawExportPanel(Arena& arena, SDL_Window* window,
                     io::Pre_Fault_Recorder* recorder = nullptr,
                     io::Replay_Controller* replayCtrl = nullptr);
} // namespace gui
