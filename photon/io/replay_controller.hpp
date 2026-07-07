#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../parse/arena.hpp"

// Forward-declare only — never include network/network.hpp in this header
struct Network;

namespace io {

enum class ReplayState { Idle, Paused, Playing };

struct ReplayFrame {
    double              timestamp_s{0.0};
    uint32_t            messageId{0};
    std::vector<double> signals;  // length == message.signalCount
};

struct LoadStats {
    bool        ok{false};
    std::string message;
    uint32_t    frameCount{0};
    double      startTime{0.0};
    double      endTime{0.0};
    uint32_t    unmatchedColumns{0};
    uint32_t    malformedRows{0};
};

struct PlaybackStatus {
    ReplayState state{ReplayState::Idle};
    double      playheadTime{0.0};
    double      startTime{0.0};     // absolute start of recording
    double      duration{0.0};      // endTime - startTime
    bool        endOfRecording{false};
};

class Replay_Controller {
public:
    // Load a Session_CSV.
    // arena is used for DBC schema lookup; network is held for suspend/resume
    // via NetworkSuspender (pointer may be null in DashboardOnly build).
    LoadStats load(const std::string& path, Arena& arena, Network* network);

    // Playback control — all safe to call regardless of state (Req 4.11)
    void play();
    void pause();
    void stop();
    void seek(double targetTime);
    void setSpeed(float speed);  // clamped to [0.1, 10.0]

    // Per-frame tick; dt is wall-clock seconds since last call.
    // Writes pending frames into arena via appendFrame.
    void tick(double dt, Arena& arena);

    PlaybackStatus status() const;
    bool           isLoaded() const { return !frames_.empty(); }

    // Returns timestamps (absolute, same units as startTime_/endTime_) where
    // a fault signal transitions from 0 → non-zero in the loaded frames.
    // Computed once on load, cached until next load.
    const std::vector<double>& faultTimestamps() const { return faultTimestamps_; }

    // Export frames within [tStart, tEnd] to a CSV file.
    // Requires a DBC-populated arena for column headers.
    // Returns an ExportResult (same structure as csv_exporter).
    struct RangeExportResult {
        bool        ok{false};
        std::string message;
        uint32_t    rowsWritten{0};
    };
    RangeExportResult exportRange(double tStart, double tEnd,
                                  const Arena& arena,
                                  const std::string& outputPath) const;

private:
    // Replay all frames up to targetTime into arena (used by seek and initial load tick)
    void doSeek(double targetTime, Arena& arena);

    // Clear all known message buffers in the arena
    void clearArena(Arena& arena);

    // Load a .photonlog binary file (dispatched from load() on .photonlog extension)
    LoadStats loadPhotonLog(const std::string& path, Arena& arena);

    // Scan frames_ for fault signal rising edges and populate faultTimestamps_
    void computeFaultTimestamps(Arena& arena);

    std::vector<ReplayFrame> frames_;
    ReplayState              state_{ReplayState::Idle};
    double                   playheadTime_{0.0};
    double                   startTime_{0.0};
    double                   endTime_{0.0};
    float                    speed_{1.0f};
    std::size_t              nextFrameIdx_{0};   // index of first un-replayed frame
    bool                     endOfRecording_{false};
    Network*                 network_{nullptr};  // held for stop() to call resumeNetwork
    Arena*                   arenaRef_{nullptr}; // held for seek after play
    std::vector<double>      faultTimestamps_;   // rising-edge fault times, computed at load
};

} // namespace io
