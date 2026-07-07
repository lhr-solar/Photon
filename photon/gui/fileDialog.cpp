// fileDialog.cpp
// Thin wrapper around SDL3's SDL_ShowOpenFileDialog / SDL_ShowSaveFileDialog.
// Uses the same callback+mutex pattern as sideBar.cpp (DBC picker).

#include "fileDialog.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <mutex>
#include <string>

namespace gui {

// ---------------------------------------------------------------------------
// Shared state — protected by a mutex because the SDL dialog callback is
// invoked on an arbitrary thread.
// ---------------------------------------------------------------------------
static std::mutex  s_openMtx;
static std::string s_openPending;   // set by callback, drained by poll
static bool        s_openHasResult = false;

static std::mutex  s_saveMtx;
static std::string s_savePending;
static bool        s_saveHasResult = false;

// ---------------------------------------------------------------------------
// Helpers: strip the "file://" scheme that SDL sometimes prepends on Windows
// ---------------------------------------------------------------------------
static std::string normalise(const char* raw) {
    if (!raw) return {};
    std::string p(raw);
    constexpr auto scheme = std::string_view("file:///");
    if (p.size() > scheme.size() && p.substr(0, scheme.size()) == scheme)
        p.erase(0, scheme.size() - 1);  // leave the leading slash for UNC
    // On Windows the path after file:/// starts with the drive letter — strip
    // the leading slash so we get "C:\..." not "\C:\..."
    if (p.size() >= 3 && p[0] == '/' && p[2] == ':')
        p.erase(0, 1);
    return p;
}

// ---------------------------------------------------------------------------
// SDL callbacks (called on an SDL internal thread)
// ---------------------------------------------------------------------------
static void SDLCALL onOpenResult(void* /*userdata*/,
                                  const char* const* filelist,
                                  int /*filter*/) {
    std::lock_guard lock(s_openMtx);
    if (filelist && filelist[0]) {
        s_openPending   = normalise(filelist[0]);
        s_openHasResult = true;
    } else {
        s_openHasResult = false;
    }
}

static void SDLCALL onSaveResult(void* /*userdata*/,
                                  const char* const* filelist,
                                  int /*filter*/) {
    std::lock_guard lock(s_saveMtx);
    if (filelist && filelist[0]) {
        s_savePending   = normalise(filelist[0]);
        s_saveHasResult = true;
    } else {
        s_saveHasResult = false;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
static const SDL_DialogFileFilter kCsvFilters[] = {
    {"CSV files", "csv"},
    {"All files", "*"},
};
static constexpr int kFilterCount = 2;

static const SDL_DialogFileFilter kReplayFilters[] = {
    {"POG files", "pog"},
    {"All files", "*"},
};
static constexpr int kReplayFilterCount = 2;

void openReplayFileDialog(SDL_Window* window) {
    // Reset any previous result so a new pick starts fresh
    { std::lock_guard lock(s_openMtx); s_openHasResult = false; s_openPending.clear(); }
    SDL_ShowOpenFileDialog(onOpenResult, nullptr, window,
                           kReplayFilters, kReplayFilterCount,
                           nullptr, false);
}

void openCsvSaveDialog(SDL_Window* window) {
    { std::lock_guard lock(s_saveMtx); s_saveHasResult = false; s_savePending.clear(); }
    SDL_ShowSaveFileDialog(onSaveResult, nullptr, window,
                           kCsvFilters, kFilterCount,
                           "session.csv");
}

static const SDL_DialogFileFilter kPhotonlogFilters[] = {
    {"POG files", "pog"},
    {"All files", "*"},
};
static constexpr int kPhotonlogFilterCount = 2;

void openPhotonlogSaveDialog(SDL_Window* window, const char* defaultName) {
    { std::lock_guard lock(s_saveMtx); s_saveHasResult = false; s_savePending.clear(); }
    SDL_ShowSaveFileDialog(onSaveResult, nullptr, window,
                           kPhotonlogFilters, kPhotonlogFilterCount,
                           defaultName ? defaultName : "recording.pog");
}

std::string pollCsvOpenPath() {
    std::lock_guard lock(s_openMtx);
    if (!s_openHasResult) return {};
    s_openHasResult = false;
    return std::move(s_openPending);
}

std::string pollCsvSavePath() {
    std::lock_guard lock(s_saveMtx);
    if (!s_saveHasResult) return {};
    s_saveHasResult = false;
    return std::move(s_savePending);
}

} // namespace gui
