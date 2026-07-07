// recorderSettings.cpp — editable recorder config controls.
// Shown inside the "Recording Settings" collapsible in replayPanel.

#include "recorderSettings.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

#include "fileDialog.hpp"
#include "imgui.h"
#include "uiComponents.hpp"

namespace gui {

namespace {

struct FolderDialogState {
    std::mutex  mtx;
    bool        active{false};
    bool        hasPending{false};
    std::string pendingPath;
};

FolderDialogState g_folderDialog;

void SDLCALL folderDialogCallback(void* userdata, const char* const* filelist, int) {
    auto* st = static_cast<FolderDialogState*>(userdata);
    if (!st) return;
    std::lock_guard lock(st->mtx);
    st->active = false;
    if (!filelist || !filelist[0]) return;
    st->pendingPath = filelist[0];
    st->hasPending  = true;
}

std::string pollFolderPath() {
    std::lock_guard lock(g_folderDialog.mtx);
    if (!g_folderDialog.hasPending) return {};
    std::string out = std::move(g_folderDialog.pendingPath);
    g_folderDialog.pendingPath.clear();
    g_folderDialog.hasPending = false;
    return out;
}

struct RecorderUiState {
    io::Pre_Fault_Recorder::Config cfg;
    char  dirBuf[256]{};
    bool  initialised{false};
    bool  saveDialogActive{false};
    int   saveCounter{1};  // increments each save to give unique default names
};

} // anonymous namespace

// ---------------------------------------------------------------------------

void drawRecorderSettings(io::Pre_Fault_Recorder& rec, Arena& /*arena*/, SDL_Window* window) {
    static RecorderUiState s;

    const PhotonUi::Palette palette = PhotonUi::palette();
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    const float gap          = ImGui::GetStyle().ItemSpacing.x;

    // Seed from live config on first draw
    if (!s.initialised) {
        s.cfg = rec.getConfig();
        if (s.cfg.pre_fault_window_s <= 0) s.cfg.pre_fault_window_s = 30;
        std::strncpy(s.dirBuf, s.cfg.log_directory.c_str(), sizeof(s.dirBuf) - 1);
        s.initialised = true;
    }

    // Re-seed dir buffer if still empty — try live recorder path first,
    // then fall back to the same OS default that photon.cpp uses.
    if (s.dirBuf[0] == '\0') {
        std::string live = rec.logDirectory();

        if (live.empty()) {
#ifdef _WIN32
            const char* up = std::getenv("USERPROFILE");
            if (up) live = std::string(up) + "\\Documents\\PhotonLogs";
            else    live = (std::filesystem::current_path() / "PhotonLogs").string();
#else
            const char* home = std::getenv("HOME");
            if (home) live = std::string(home) + "/Documents/PhotonLogs";
            else      live = (std::filesystem::current_path() / "PhotonLogs").string();
#endif
        }

        std::strncpy(s.dirBuf, live.c_str(), sizeof(s.dirBuf) - 1);
        s.dirBuf[sizeof(s.dirBuf) - 1] = '\0';
        s.cfg.log_directory = s.dirBuf;
    }

    // Poll async folder dialog result
    {
        std::string picked = pollFolderPath();
        if (!picked.empty()) {
            std::strncpy(s.dirBuf, picked.c_str(), sizeof(s.dirBuf) - 1);
            s.dirBuf[sizeof(s.dirBuf) - 1] = '\0';
            s.cfg.log_directory = s.dirBuf;
        }
    }

    // Poll save dialog result — when user confirms, seal to that path
    {
        std::string picked = gui::pollCsvSavePath();
        if (!picked.empty()) {
            s.saveDialogActive = false;
            // Ensure .photonlog extension
            namespace fs = std::filesystem;
            fs::path p(picked);
            if (p.extension() != ".pog")
                p.replace_extension(".pog");
            rec.triggerSealTo(p.string());
            ++s.saveCounter;
        }
    }

    // ── Enable toggle (ON / OFF buttons) + Save ──────────────────────────────
    {
        const auto curState = rec.state();
        const bool isOn     = (curState == io::Pre_Fault_Recorder::State::Recording ||
                                curState == io::Pre_Fault_Recorder::State::Sealing);
        const bool isRecording = (curState == io::Pre_Fault_Recorder::State::Recording);
        const float saveW  = 60.0f;
        const float toggleW = (contentWidth - saveW - gap * 2.0f) * 0.5f;

        if (PhotonUi::button("RecOn", "ON", {toggleW, 28.0f}, palette, isOn)) {
            if (!isOn) {
                s.cfg.enabled = true;
                rec.reconfigure(s.cfg);
            }
        }
        ImGui::SameLine(0.0f, gap);
        if (PhotonUi::button("RecOff", "OFF", {toggleW, 28.0f}, palette, !isOn)) {
            if (isOn) {
                rec.triggerSeal();
                s.cfg.enabled = false;
                rec.reconfigure(s.cfg);
            }
        }
        ImGui::SameLine(0.0f, gap);
        ImGui::BeginDisabled(!isRecording);
        if (PhotonUi::button("RecSave", "Save", {saveW, 28.0f}, palette)) {
            if (!s.saveDialogActive) {
                s.saveDialogActive = true;
                // Build default name: recording_001.photonlog, skipping names that exist
                char defaultName[64];
                namespace fs = std::filesystem;
                const std::string& dir = rec.logDirectory();
                do {
                    std::snprintf(defaultName, sizeof(defaultName),
                                  "recording_%03d.pog", s.saveCounter);
                    if (dir.empty()) break;
                    fs::path candidate = fs::path(dir) / defaultName;
                    if (!fs::exists(candidate)) break;
                    ++s.saveCounter;
                } while (s.saveCounter < 10000);
                gui::openPhotonlogSaveDialog(window, defaultName);
            }
        }
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    // ── Pre-fault window ─────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Text, palette.muted);
    ImGui::TextUnformatted("Pre-fault window");
    ImGui::PopStyleColor();
    {
        int win = s.cfg.pre_fault_window_s;
        PhotonUi::pushInputStyle(palette);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderInt("##pfw", &win, 5, 300, "%d s")) {
            win = (win < 5) ? 5 : (win > 300 ? 300 : win);
            s.cfg.pre_fault_window_s = win;
        }
        PhotonUi::popInputStyle();
    }

    ImGui::Spacing();

    // ── Log directory ────────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Text, palette.muted);
    ImGui::TextUnformatted("Log directory");
    ImGui::PopStyleColor();
    {
        const float browseW = 72.0f;
        PhotonUi::pushInputStyle(palette);
        ImGui::SetNextItemWidth(contentWidth - browseW - gap);
        if (ImGui::InputText("##logDir", s.dirBuf, sizeof(s.dirBuf)))
            s.cfg.log_directory = s.dirBuf;
        PhotonUi::popInputStyle();
        ImGui::SameLine(0.0f, gap);

        bool dialogActive = false;
        { std::lock_guard lock(g_folderDialog.mtx); dialogActive = g_folderDialog.active; }

        if (PhotonUi::button("BrowseLogDir",
                             dialogActive ? "..." : "Browse\xe2\x80\xa6",
                             {browseW, 26.0f}, palette)) {
            if (!dialogActive) {
                std::lock_guard lock(g_folderDialog.mtx);
                g_folderDialog.active     = true;
                g_folderDialog.hasPending = false;
                g_folderDialog.pendingPath.clear();
                SDL_ShowOpenFolderDialog(folderDialogCallback, &g_folderDialog,
                                         window, nullptr, false);
            }
        }
    }

    ImGui::Spacing();

    // ── Retry (Error state only) + Apply ─────────────────────────────────────
    {
        const auto curState = rec.state();
        if (curState == io::Pre_Fault_Recorder::State::Error) {
            if (PhotonUi::button("RecorderRetry", "Retry", {72.0f, 26.0f}, palette)) {
                s.cfg.log_directory = s.dirBuf;
                rec.init(s.cfg);
            }
            ImGui::SameLine(0.0f, gap);
        }

        if (PhotonUi::button("RecorderApply", "Apply", {72.0f, 26.0f}, palette)) {
            s.cfg.log_directory = s.dirBuf;
            rec.reconfigure(s.cfg);
        }
    }
}

} // namespace gui
