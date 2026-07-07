// photon/gui/replayPanel.cpp
// Combined Recording + Replay tab panel.

#include <cstdio>
#include <cstring>
#include <ctime>

#include "../io/pre_fault_recorder.hpp"
#include "../io/replay_controller.hpp"
#include "fileDialog.hpp"
#include "imgui.h"
#include "recorderSettings.hpp"
#include "replayPanel.hpp"
#include "uiComponents.hpp"

namespace gui {

// Format a Unix timestamp (seconds) as  HH:MM:SS  (local time, no date).
static void fmtTime(char* buf, int bufsz, double unix_s) {
    std::time_t t = static_cast<std::time_t>(unix_s);
    std::tm*    tm = std::localtime(&t);
    if (tm)
        std::snprintf(buf, bufsz, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    else
        std::snprintf(buf, bufsz, "--:--:--");
}

// Format an elapsed duration in seconds as  M:SS.mmm
static void fmtElapsed(char* buf, int bufsz, double elapsed_s) {
    if (elapsed_s < 0.0) elapsed_s = 0.0;
    int    mins  = static_cast<int>(elapsed_s) / 60;
    double secs  = elapsed_s - mins * 60.0;
    std::snprintf(buf, bufsz, "%d:%06.3f", mins, secs);
}

void drawReplayPanel(io::Replay_Controller& rc, Arena& arena, Network* network,
                     SDL_Window* window,
                     io::Pre_Fault_Recorder* recorder) {
    static char          s_pathBuf[256]{};
    static io::LoadStats s_lastStats{};
    static bool          s_browseActive{false};
    static float         s_speed{1.0f};

    const PhotonUi::Palette palette = PhotonUi::palette();
    const ImVec2            avail   = ImGui::GetContentRegionAvail();
    const float             gap     = ImGui::GetStyle().ItemSpacing.x;

    // Poll open-file dialog
    {
        std::string picked = gui::pollCsvOpenPath();  // shared open-result slot
        if (!picked.empty()) {
            std::strncpy(s_pathBuf, picked.c_str(), sizeof(s_pathBuf) - 1);
            s_pathBuf[sizeof(s_pathBuf) - 1] = '\0';
            s_browseActive = false;
        }
    }

    const bool hasRecorder = (recorder != nullptr);
    const float leftW  = hasRecorder ? std::max(160.0f, (avail.x - gap) * 0.50f) : 0.0f;
    const float rightW = hasRecorder ? (avail.x - leftW - gap) : avail.x;

    // =========================================================================
    // ── LEFT COLUMN: Recording status  +  Recording settings ─────────────────
    // =========================================================================
    if (hasRecorder) {
        ImGui::BeginGroup();

        const auto recState    = recorder->state();

        const char* badgeLabel = "Disabled";
        ImVec4      badgeColor = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
        switch (recState) {
            case io::Pre_Fault_Recorder::State::Recording:
                badgeLabel = "\xe2\x97\x8f Recording";
                badgeColor = ImVec4(0.25f, 0.85f, 0.42f, 1.0f); break;
            case io::Pre_Fault_Recorder::State::Sealing:
                badgeLabel = "\xe2\x8f\xba Sealing\xe2\x80\xa6";
                badgeColor = ImVec4(0.95f, 0.78f, 0.20f, 1.0f); break;
            case io::Pre_Fault_Recorder::State::Error:
                badgeLabel = "\xe2\x9c\x97 Error";
                badgeColor = ImVec4(0.95f, 0.35f, 0.35f, 1.0f); break;
            default: break;
        }

        // ── Single combined Recording panel ──────────────────────────────────
        if (PhotonUi::beginPanel("##RecordingPanel", {leftW, 0.0f}, palette,
                                  ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
            PhotonUi::label("Recording", palette);
            ImGui::SameLine(0.0f, gap);
            ImGui::PushStyleColor(ImGuiCol_Text, badgeColor);
            ImGui::TextUnformatted(badgeLabel);
            ImGui::PopStyleColor();

            const uint64_t dropped = recorder->droppedFrames();
            if (dropped > 0) {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "Dropped: %llu",
                              static_cast<unsigned long long>(dropped));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.55f, 0.20f, 1.0f));
                ImGui::TextUnformatted(buf);
                ImGui::PopStyleColor();
            }

            ImGui::Dummy({0.0f, 2.0f});

            if (recState == io::Pre_Fault_Recorder::State::Sealing) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.78f, 0.20f, 1.0f));
                ImGui::TextUnformatted("Sealing\xe2\x80\xa6");
                ImGui::PopStyleColor();
            } else if (recState == io::Pre_Fault_Recorder::State::Error) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                ImGui::TextWrapped("%s", recorder->lastError().c_str());
                ImGui::PopStyleColor();
            } else if (recState == io::Pre_Fault_Recorder::State::Disabled) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
                ImGui::TextWrapped("Enable in Settings below");
                ImGui::PopStyleColor();
            }

            ImGui::Dummy({0.0f, 2.0f});
            ImGui::Separator();
            ImGui::Dummy({0.0f, 2.0f});

            gui::drawRecorderSettings(*recorder, arena, window);
        }
        PhotonUi::endPanel();

        ImGui::EndGroup();
        ImGui::SameLine(0.0f, gap);
    }

    // =========================================================================
    // ── RIGHT COLUMN: Replay load + transport ────────────────────────────────
    // =========================================================================
    if (PhotonUi::beginPanel("##ReplayPanel", {rightW, 0.0f}, palette,
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        PhotonUi::label("Replay", palette);
        ImGui::Dummy({0.0f, 2.0f});

        const float innerW = ImGui::GetContentRegionAvail().x;
        const float btnW   = 64.0f;
        PhotonUi::pushInputStyle(palette);
        ImGui::SetNextItemWidth(innerW - btnW * 2.0f - gap * 2.0f);
        ImGui::InputText("##replayPath", s_pathBuf, sizeof(s_pathBuf));
        PhotonUi::popInputStyle();

        ImGui::SameLine(0.0f, gap);
        if (PhotonUi::button("BrowseReplay",
                             s_browseActive ? "..." : "Browse\xe2\x80\xa6",
                             {btnW, 24.0f}, palette)) {
            if (!s_browseActive) {
                s_browseActive = true;
                gui::openReplayFileDialog(window);
            }
        }
        ImGui::SameLine(0.0f, gap);
        if (PhotonUi::button("LoadBtn", "Load", {btnW, 24.0f}, palette)) {
            s_lastStats    = rc.load(std::string(s_pathBuf), arena, network);
            s_browseActive = false;
        }

        if (s_lastStats.ok) {
            char tStart[16], tEnd[16];
            fmtTime(tStart, sizeof(tStart), s_lastStats.startTime);
            fmtTime(tEnd,   sizeof(tEnd),   s_lastStats.endTime);
            const double dur = s_lastStats.endTime - s_lastStats.startTime;
            char durBuf[16];
            fmtElapsed(durBuf, sizeof(durBuf), dur);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%u frames  %s \xe2\x80\x93 %s  (%s)",
                          s_lastStats.frameCount, tStart, tEnd, durBuf);
            ImGui::PushStyleColor(ImGuiCol_Text, palette.accent);
            ImGui::TextUnformatted(buf);
            ImGui::PopStyleColor();
        } else if (!s_lastStats.message.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.32f, 0.32f, 1.0f));
            ImGui::TextUnformatted(s_lastStats.message.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Dummy({0.0f, 2.0f});
        ImGui::Separator();
        ImGui::Dummy({0.0f, 2.0f});

        const bool loaded    = rc.isLoaded();
        const auto status    = rc.status();
        const bool isPlaying = (status.state == io::ReplayState::Playing);

        ImGui::BeginDisabled(!loaded);

        if (PhotonUi::button("PlayPauseBtn", isPlaying ? "Pause" : "Play",
                             {64.0f, 26.0f}, palette, isPlaying)) {
            if (isPlaying) rc.pause(); else rc.play();
        }
        ImGui::SameLine(0.0f, gap);
        if (PhotonUi::button("StopBtn", "Stop", {52.0f, 26.0f}, palette))
            rc.stop();
        ImGui::SameLine(0.0f, gap * 2.0f);
        ImGui::SetNextItemWidth(72.0f);
        if (ImGui::InputFloat("Speed", &s_speed, 0.1f, 1.0f, "%.2fx")) {
            s_speed = s_speed < 0.1f ? 0.1f : s_speed > 10.0f ? 10.0f : s_speed;
            rc.setSpeed(s_speed);
        }

        ImGui::Spacing();
        {
            double playhead = status.playheadTime;
            double tStart   = s_lastStats.startTime;
            double tEnd     = (s_lastStats.endTime > s_lastStats.startTime)
                                  ? s_lastStats.endTime
                                  : s_lastStats.startTime + 1.0;

            // Slider operates on elapsed seconds for a clean 0-based range
            double elapsed    = playhead - tStart;
            double duration   = tEnd - tStart;
            double elapsedMin = 0.0;

            // Format label: M:SS.mmm / M:SS.mmm
            char elBuf[16], durBuf[16];
            fmtElapsed(elBuf,  sizeof(elBuf),  elapsed);
            fmtElapsed(durBuf, sizeof(durBuf), duration);
            char sliderLabel[48];
            std::snprintf(sliderLabel, sizeof(sliderLabel), "%s / %s", elBuf, durBuf);

            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderScalar("##seek", ImGuiDataType_Double,
                                    &elapsed, &elapsedMin, &duration, sliderLabel))
                rc.seek(tStart + elapsed);

            // ── Fault markers: red vertical lines overlaid on the slider track ──
            const auto& faults = rc.faultTimestamps();
            if (!faults.empty() && duration > 0.0) {
                const ImVec2 sliderMin = ImGui::GetItemRectMin();
                const ImVec2 sliderMax = ImGui::GetItemRectMax();
                ImDrawList*  dl        = ImGui::GetWindowDrawList();

                for (double ft : faults) {
                    const double t = ft - tStart;
                    if (t < 0.0 || t > duration) continue;
                    const float frac = static_cast<float>(t / duration);
                    const float x    = sliderMin.x + frac * (sliderMax.x - sliderMin.x);
                    dl->AddLine({x, sliderMin.y},
                                {x, sliderMax.y},
                                IM_COL32(220, 40, 40, 220), 2.0f);
                }
            }
        }

        ImGui::EndDisabled();

        if (status.endOfRecording) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.76f, 0.20f, 1.0f));
            ImGui::TextUnformatted("\xe2\x8f\xb9  End of recording");
            ImGui::PopStyleColor();
        }
    }
    PhotonUi::endPanel();
}

} // namespace gui
