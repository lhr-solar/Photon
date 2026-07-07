#include "exportPanel.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "../io/csv_exporter.hpp"
#include "../io/pre_fault_recorder.hpp"
#include "../io/replay_controller.hpp"
#include "fileDialog.hpp"
#include "imgui.h"
#include "uiComponents.hpp"

namespace gui {

// ============================================================================
// drawExportPanel
// ============================================================================

void drawExportPanel(Arena& arena, SDL_Window* window,
                     io::Pre_Fault_Recorder* recorder,
                     io::Replay_Controller* replayCtrl) {
    // ── Per-call state ─────────────────────────────────────────────────────
    struct FileExportState {
        std::string path;
        bool        exporting{false};
        bool        attempted{false};
        bool        ok{false};
        std::string message;
    };

    struct State {
        // CSV export section
        char             csvPath[256]{};
        io::ExportResult lastResult{};
        bool             attempted{false};
        bool             browseActive{false};

        // Per-file recording exports (keyed by filename for stable identity)
        std::vector<FileExportState> fileStates;
    };
    static State s;

    const PhotonUi::Palette palette = PhotonUi::palette();
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    const float gap          = ImGui::GetStyle().ItemSpacing.x;

    // ========================================================================
    // ── Fault Snapshot ───────────────────────────────────────────────────────
    // ========================================================================
    if (recorder) {
        ImGui::Separator();
        ImGui::Dummy({0.0f, 4.0f});
        PhotonUi::label("Fault Snapshot", palette);
        ImGui::Dummy({0.0f, 4.0f});

        const auto recState = recorder->state();

        if (recState == io::Pre_Fault_Recorder::State::Sealing) {
            // Non-interactive indicator while sealing
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.80f, 0.20f, 1.0f));
            ImGui::TextUnformatted("Sealing\xe2\x80\xa6");
            ImGui::PopStyleColor();
        } else {
            const bool canSeal = (recState == io::Pre_Fault_Recorder::State::Recording);
            if (PhotonUi::button("SaveFaultSnapshotBtn", "Save Fault Snapshot",
                                 {contentWidth, 34.0f}, palette,
                                 /*disabled=*/!canSeal)) {
                recorder->triggerSeal();
            }
            if (!canSeal) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.60f, 1.0f));
                if (recState == io::Pre_Fault_Recorder::State::Disabled)
                    ImGui::TextUnformatted("Recorder is disabled");
                else if (recState == io::Pre_Fault_Recorder::State::Error)
                    ImGui::TextUnformatted("Recorder error — check Settings");
                ImGui::PopStyleColor();
            }
        }

        ImGui::Dummy({0.0f, 4.0f});
    }

    // ========================================================================
    // ── Recordings ──────────────────────────────────────────────────────────
    // ========================================================================
    if (recorder && replayCtrl) {
        ImGui::Separator();
        ImGui::Dummy({0.0f, 4.0f});
        PhotonUi::label("Recordings", palette);
        ImGui::Dummy({0.0f, 4.0f});

        const std::string logDir = recorder->logDirectory();

        // Gather .photonlog files
        std::vector<std::string> logFiles;
        if (!logDir.empty()) {
            std::error_code ec;
            for (const auto& entry :
                 std::filesystem::directory_iterator(logDir, ec)) {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;
                if (entry.path().extension() == ".pog") {
                    logFiles.push_back(entry.path().string());
                }
            }
        }

        // Sort descending by filename (newest first via timestamp in name)
        std::sort(logFiles.begin(), logFiles.end(),
                  [](const std::string& a, const std::string& b) {
                      namespace fs = std::filesystem;
                      return fs::path(a).filename().string() >
                             fs::path(b).filename().string();
                  });

        // Sync per-file state vector to current file list
        // (keep existing state for files still present, drop stale entries)
        {
            std::vector<FileExportState> newStates;
            newStates.reserve(logFiles.size());
            for (const auto& p : logFiles) {
                auto it = std::find_if(s.fileStates.begin(), s.fileStates.end(),
                                       [&p](const FileExportState& fs) {
                                           return fs.path == p;
                                       });
                if (it != s.fileStates.end()) {
                    newStates.push_back(std::move(*it));
                } else {
                    FileExportState fes;
                    fes.path = p;
                    newStates.push_back(std::move(fes));
                }
            }
            s.fileStates = std::move(newStates);
        }

        if (logFiles.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.60f, 1.0f));
            ImGui::TextUnformatted("No recordings found");
            ImGui::PopStyleColor();
        } else {
            for (std::size_t i = 0; i < s.fileStates.size(); ++i) {
                FileExportState& fes = s.fileStates[i];
                namespace fs = std::filesystem;

                const std::string filename =
                    fs::path(fes.path).filename().string();

                // Filename label
                ImGui::TextUnformatted(filename.c_str());

                // Build a unique ImGui ID for the button
                char btnId[64];
                std::snprintf(btnId, sizeof(btnId), "ExportCSV_%zu", i);

                ImGui::SameLine(0.0f, gap);

                if (PhotonUi::button(btnId, "Export to CSV",
                                     {120.0f, 24.0f}, palette,
                                     /*disabled=*/fes.exporting)) {
                    fes.exporting = true;
                    fes.attempted = false;
                    fes.ok        = false;
                    fes.message.clear();

                    // Derive CSV output path: same name, .csv extension
                    fs::path csvPath = fs::path(fes.path);
                    csvPath.replace_extension(".csv");

                    // Load photonlog into arena via replay controller
                    const auto loadStats =
                        replayCtrl->load(fes.path, arena, nullptr);

                    if (!loadStats.ok) {
                        fes.ok        = false;
                        fes.message   = "Load failed: " + loadStats.message;
                        fes.attempted = true;
                        fes.exporting = false;
                    } else {
                        const io::ExportResult result =
                            io::exportArena(arena, csvPath.string());
                        fes.ok        = result.ok;
                        fes.message   = result.ok
                            ? result.outputPath
                            : result.message;
                        fes.attempted = true;
                        fes.exporting = false;
                    }
                }

                // Status text for this file
                if (fes.attempted) {
                    ImGui::SameLine(0.0f, gap);
                    if (fes.ok) {
                        ImGui::PushStyleColor(ImGuiCol_Text,
                                              ImVec4(0.36f, 0.87f, 0.50f, 1.0f));
                        ImGui::TextUnformatted(fes.message.c_str());
                        ImGui::PopStyleColor();
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text,
                                              ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                        ImGui::TextUnformatted(fes.message.c_str());
                        ImGui::PopStyleColor();
                    }
                }
            }
        }

        ImGui::Dummy({0.0f, 4.0f});
    }

    // ========================================================================
    // ── CSV Export ──────────────────────────────────────────────────────────
    // ========================================================================
    ImGui::Separator();
    ImGui::Dummy({0.0f, 4.0f});

    // Poll for a path chosen by the save dialog
    {
        std::string picked = gui::pollCsvSavePath();
        if (!picked.empty()) {
            std::strncpy(s.csvPath, picked.c_str(), sizeof(s.csvPath) - 1);
            s.csvPath[sizeof(s.csvPath) - 1] = '\0';
            s.browseActive = false;
        }
    }

    // File path input + Browse button
    PhotonUi::pushInputStyle(palette);
    ImGui::SetNextItemWidth(contentWidth - 88.0f - gap);
    ImGui::InputText("##exportPath", s.csvPath, sizeof(s.csvPath));
    PhotonUi::popInputStyle();

    ImGui::SameLine(0.0f, gap);
    if (PhotonUi::button("BrowseExport",
                         s.browseActive ? "..." : "Browse\xe2\x80\xa6",
                         {82.0f, 28.0f}, palette)) {
        if (!s.browseActive) {
            s.browseActive = true;
            gui::openCsvSaveDialog(window);
        }
    }

    ImGui::Dummy({0.0f, 4.0f});

    // Export button
    if (PhotonUi::button("ExportBtn", "Export", {96.0f, 34.0f}, palette)) {
        s.lastResult = io::exportArena(arena, s.csvPath);
        s.attempted  = true;
    }

    // Status line — only shown after at least one export attempt (Req 2.8)
    if (s.attempted) {
        ImGui::Dummy({0.0f, 6.0f});

        if (s.lastResult.ok) {
            char buf[320];
            std::snprintf(buf, sizeof(buf), "%u rows \xe2\x86\x92 %s",
                          s.lastResult.rowsWritten,
                          s.lastResult.outputPath.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.36f, 0.87f, 0.50f, 1.0f));
            ImGui::TextUnformatted(buf);
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
            ImGui::TextUnformatted(s.lastResult.message.c_str());
            ImGui::PopStyleColor();
        }
    }
}

} // namespace gui
