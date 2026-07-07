#pragma once
#include "../io/pre_fault_recorder.hpp"
#include "../parse/arena.hpp"

struct SDL_Window;

namespace gui {
// Renders the Pre-Fault Recorder settings section.
// Call from GUI::settingsUI() inside the modal.
void drawRecorderSettings(io::Pre_Fault_Recorder& rec, Arena& arena, SDL_Window* window);
} // namespace gui
