#pragma once
#include <string>

// Forward-declare SDL_Window so callers don't need to include SDL headers
struct SDL_Window;

namespace gui {

// Asynchronous SDL file-open dialog for CSV files.
// Call openCsvFileDialog() once to launch the picker; it returns immediately.
// Each frame, poll pendingCsvPath() — it returns the chosen path (non-empty)
// exactly once after the user confirms, then clears itself.
// Pass the SDL_Window* so the dialog is parented to the Photon window.

void        openReplayFileDialog(SDL_Window* window); // open .photonlog picker (async)
void        openCsvSaveDialog(SDL_Window* window);    // CSV save picker (async)
void        openPhotonlogSaveDialog(SDL_Window* window, const char* defaultName); // .photonlog save picker (async)
std::string pollCsvOpenPath();  // drain the open result (empty = not ready yet)
std::string pollCsvSavePath();  // drain the save result

} // namespace gui
