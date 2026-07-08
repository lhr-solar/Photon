// replay_controller.cpp
// Implements load (Task 5.2), playback control (Task 5.3), and tick loop (Task 5.4).
// Extended in Task 6.1/6.2 to support .photonlog binary loading.
// Requirements: 3.1–3.7, 4.1–4.11, 7.1–7.7
// No includes from network/ or synth/ — all Network interaction goes via network_suspender.hpp.

#include "replay_controller.hpp"
#include "network_suspender.hpp"
#include "photonlog.hpp"
#include "../engine/include.hpp"  // logs() macro

#include <algorithm>    // std::stable_sort, std::min
#include <cstdio>       // fopen, fread, fclose
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace io {

// ---------------------------------------------------------------------------
// Task 5.2 / 6.1 — load()
// Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 7.1
// ---------------------------------------------------------------------------
LoadStats Replay_Controller::load(const std::string& path, Arena& arena, Network* network) {
    namespace fs = std::filesystem;

    network_ = network;

    // Only .pog files are supported for replay
    if (fs::path(path).extension() != ".pog") {
        LoadStats stats;
        stats.ok      = false;
        stats.message = "Unsupported file type — only .pog files can be loaded for replay";
        return stats;
    }

    return loadPhotonLog(path, arena);
}

// ---------------------------------------------------------------------------
// Task 5.3 — Playback control
// Requirements: 4.1, 4.4–4.8, 4.10, 4.11
// ---------------------------------------------------------------------------

void Replay_Controller::play() {
    // Req 4.11: guard — no recording loaded
    if (frames_.empty()) {
        logs("Replay_Controller::play() — no recording loaded, ignoring");
        return;
    }

    if (state_ == ReplayState::Playing) return;  // already playing, no-op

    // Req 4.1: suspend network on transition to Playing
    if (network_) {
        io::suspendNetwork(*network_);
    }

    // Clear end-of-recording flag so playback can proceed
    endOfRecording_ = false;

    state_ = ReplayState::Playing;
}

void Replay_Controller::pause() {
    // Req 4.11: guard — no recording loaded
    if (frames_.empty()) {
        logs("Replay_Controller::pause() — no recording loaded, ignoring");
        return;
    }

    if (state_ == ReplayState::Paused || state_ == ReplayState::Idle) return;

    // Req 4.4: stop advancing playhead
    state_ = ReplayState::Paused;
}

void Replay_Controller::stop() {
    // Req 4.11: guard — no recording loaded
    if (frames_.empty()) {
        logs("Replay_Controller::stop() — no recording loaded, ignoring");
        return;
    }

    // Req 4.10: resume network and return to Live_Mode
    if (network_) {
        io::resumeNetwork(*network_);
    }

    // Clear all replay state
    frames_.clear();
    state_          = ReplayState::Idle;
    playheadTime_   = 0.0;
    startTime_      = 0.0;
    endTime_        = 0.0;
    nextFrameIdx_   = 0;
    endOfRecording_ = false;
    network_        = nullptr;
    arenaRef_       = nullptr;
}

void Replay_Controller::setSpeed(float speed) {
    // Req 4.2: clamp to [0.1, 10.0]
    if (speed < 0.1f) speed = 0.1f;
    if (speed > 10.0f) speed = 10.0f;
    speed_ = speed;
}

void Replay_Controller::seek(double targetTime) {
    // Req 4.11: guard — no recording loaded
    if (frames_.empty()) {
        logs("Replay_Controller::seek() — no recording loaded, ignoring");
        return;
    }

    // Req 4.8: clamp to [startTime_, endTime_]
    if (targetTime < startTime_) targetTime = startTime_;
    if (targetTime > endTime_)   targetTime = endTime_;

    // Req 4.5: clear arena then replay frames up to targetTime
    if (arenaRef_) {
        clearArena(*arenaRef_);
    }

    playheadTime_   = targetTime;
    nextFrameIdx_   = 0;
    endOfRecording_ = false;

    if (arenaRef_) {
        doSeek(targetTime, *arenaRef_);
    }

    // Req 4.6: if playing, remain playing (seek during play → resume from target)
    // Req 4.7: if paused, remain paused at target
    // state_ is unchanged — the caller's current state is preserved.
}

// ---------------------------------------------------------------------------
// Task 5.4 — Tick loop
// Requirements: 4.2, 4.3, 4.9
// ---------------------------------------------------------------------------

void Replay_Controller::tick(double dt, Arena& arena) {
    if (state_ != ReplayState::Playing) return;
    if (frames_.empty()) return;

    // Req 4.2: advance playhead by speed * dt
    playheadTime_ += static_cast<double>(speed_) * dt;

    // Req 4.3: inject all frames whose timestamp_s <= playheadTime_
    while (nextFrameIdx_ < frames_.size()) {
        const ReplayFrame& frame = frames_[nextFrameIdx_];
        if (frame.timestamp_s > playheadTime_) break;

        bool ok = arena.appendFrame(
            frame.messageId,
            frame.timestamp_s,
            frame.signals.data(),
            static_cast<uint32_t>(frame.signals.size()));

        if (!ok) {
            logs("Replay_Controller::tick() — appendFrame returned false for messageId "
                 << frame.messageId << " at t=" << frame.timestamp_s << ", skipping frame");
        }

        ++nextFrameIdx_;
    }

    // Req 4.9: detect end-of-recording
    if (nextFrameIdx_ >= frames_.size()) {
        endOfRecording_ = true;
        state_          = ReplayState::Paused;
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Task 6.2 — loadPhotonLog()
// Requirements: 7.2, 7.3, 7.4, 7.6, 7.7, 3.6
// ---------------------------------------------------------------------------
LoadStats Replay_Controller::loadPhotonLog(const std::string& path, Arena& arena) {
    LoadStats stats;

    // 1. Open file in binary read mode
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        stats.ok      = false;
        stats.message = "Cannot open .pog file: '" + path + "'";
        return stats;
    }

    // 2. Read the header
    PhotonLog_Header header{};
    if (std::fread(&header, sizeof(PhotonLog_Header), 1, fp) != 1) {
        std::fclose(fp);
        stats.ok      = false;
        stats.message = "Failed to read header from .pog file: '" + path + "'";
        return stats;
    }

    // 3. Validate magic and version (Req 7.2, 3.6)
    if (header.magic != PLOG_MAGIC) {
        std::fclose(fp);
        stats.ok      = false;
        stats.message = "Invalid .pog file: bad magic (expected 0x504C4F47, got 0x"
                        + [&]{ char buf[16]; std::snprintf(buf, sizeof(buf), "%08X", header.magic); return std::string(buf); }()
                        + "): '" + path + "'";
        return stats;
    }
    if (header.version != PLOG_VERSION) {
        std::fclose(fp);
        stats.ok      = false;
        stats.message = "Invalid .pog file: unsupported version (expected 0x0001, got 0x"
                        + [&]{ char buf[8]; std::snprintf(buf, sizeof(buf), "%04X", header.version); return std::string(buf); }()
                        + "): '" + path + "'";
        return stats;
    }

    const uint64_t capacity = header.record_capacity;

    // Guard against degenerate files
    if (capacity == 0 && header.write_cursor == 0) {
        std::fclose(fp);
        stats.ok      = true;
        stats.message = "Loaded 0 frames from '" + path + "' (empty recording)";
        return stats;
    }

    // Unbounded recording: record_capacity == 0, write_cursor == total frames written.
    const bool unbounded  = (capacity == 0);
    const uint64_t nSlots = unbounded ? header.write_cursor : capacity;

    // 4. Read all record slots at once (Req 7.3, 7.4)
    std::vector<PhotonLog_Record> records(static_cast<std::size_t>(nSlots));
    const std::size_t nRead = std::fread(records.data(),
                                         sizeof(PhotonLog_Record),
                                         static_cast<std::size_t>(nSlots),
                                         fp);
    std::fclose(fp);

    if (nRead != static_cast<std::size_t>(nSlots)) {
        stats.ok      = false;
        stats.message = "Unexpected end of .pog file '" + path
                        + "': expected " + std::to_string(nSlots)
                        + " records, got " + std::to_string(nRead);
        return stats;
    }

    // 5. Reconstruct chronological order from circular log (Req 7.3, 7.4, 7.5)
    // For unbounded files the records are already in order (linear append),
    // so oldest_slot = 0.  For circular files oldest_slot = write_cursor % capacity.
    const uint64_t oldest_slot = unbounded ? 0 : (header.write_cursor % capacity);

    uint32_t unmatchedColumns = 0;
    uint32_t malformedRows    = 0;

    std::vector<ReplayFrame> frames;
    frames.reserve(static_cast<std::size_t>(capacity));

    for (uint64_t i = 0; i < nSlots; ++i) {
        const uint64_t slot_idx = (oldest_slot + i) % nSlots;
        const PhotonLog_Record& rec = records[static_cast<std::size_t>(slot_idx)];

        // Skip malformed records (signal_count == 0) — Req 7.7
        if (rec.signal_count == 0) {
            ++malformedRows;
            continue;
        }

        // Skip records whose msg_id is not in arena.validIds — Req 7.6
        bool idFound = false;
        for (uint32_t vid : arena.validIds) {
            if (vid == rec.msg_id) { idFound = true; break; }
        }
        if (!idFound) {
            ++unmatchedColumns;
            continue;
        }

        // Build ReplayFrame
        ReplayFrame frame;
        frame.timestamp_s = rec.timestamp_s;
        frame.messageId   = rec.msg_id;

        const uint32_t sigCount = rec.signal_count <= SIGNAL_MAX
                                ? rec.signal_count
                                : SIGNAL_MAX;
        frame.signals.assign(rec.signals, rec.signals + sigCount);

        frames.push_back(std::move(frame));
    }

    // 6. Stable sort by timestamp_s (Req 7.3 — chronological order)
    std::stable_sort(frames.begin(), frames.end(),
                     [](const ReplayFrame& a, const ReplayFrame& b) {
                         return a.timestamp_s < b.timestamp_s;
                     });

    // 7. Populate stats and commit state (mirrors end of CSV load path)
    if (frames.empty()) {
        stats.startTime = 0.0;
        stats.endTime   = 0.0;
    } else {
        stats.startTime = frames.front().timestamp_s;
        stats.endTime   = frames.back().timestamp_s;
    }

    stats.ok               = true;
    stats.frameCount       = static_cast<uint32_t>(frames.size());
    stats.unmatchedColumns = unmatchedColumns;
    stats.malformedRows    = malformedRows;
    stats.message          = "Loaded " + std::to_string(stats.frameCount)
                             + " frames from '" + path + "'";

    // Commit state — transition to Paused
    frames_         = std::move(frames);
    startTime_      = stats.startTime;
    endTime_        = stats.endTime;
    playheadTime_   = startTime_;
    nextFrameIdx_   = 0;
    endOfRecording_ = false;
    state_          = ReplayState::Paused;
    arenaRef_       = &arena;
    // network_ was already set by load() before dispatching here

    computeFaultTimestamps(arena);

    return stats;
}

// Replay all frames from index 0 up to (and including) targetTime into arena.
// Used by seek() to rebuild arena state at a target timestamp.
void Replay_Controller::doSeek(double targetTime, Arena& arena) {
    nextFrameIdx_ = 0;
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        const ReplayFrame& frame = frames_[i];
        if (frame.timestamp_s > targetTime) {
            nextFrameIdx_ = i;
            return;
        }

        bool ok = arena.appendFrame(
            frame.messageId,
            frame.timestamp_s,
            frame.signals.data(),
            static_cast<uint32_t>(frame.signals.size()));

        if (!ok) {
            logs("Replay_Controller::doSeek() — appendFrame returned false for messageId "
                 << frame.messageId << " at t=" << frame.timestamp_s << ", skipping frame");
        }
    }
    // All frames are <= targetTime
    nextFrameIdx_ = frames_.size();
}

// Clear all known message buffers in the arena.
void Replay_Controller::clearArena(Arena& arena) {
    for (uint32_t id : arena.validIds) {
        arena.clear(id);
    }
}

// ---------------------------------------------------------------------------
// Fault timestamp detection
// Mirrors the hardcoded fault-signal table in Pre_Fault_Recorder.
// ---------------------------------------------------------------------------
void Replay_Controller::computeFaultTimestamps(Arena& arena) {
    faultTimestamps_.clear();

    // Same fault table as Pre_Fault_Recorder::resolveFaultSignals()
    struct Entry { uint32_t msg_id; const char* sig_name; };
    static constexpr Entry kTable[] = {
        {  1u, "BPS_Fault"  },
        { 24u, "VCU_Fault"  },
        {513u, "MPPT_Fault" },
        {529u, "MPPT_Fault" },
        {545u, "MPPT_Fault" },
    };
    static constexpr uint32_t kTableSize =
        static_cast<uint32_t>(sizeof(kTable) / sizeof(kTable[0]));

    // Resolve signal indices against the arena schema
    struct Resolved { uint32_t msg_id; uint32_t sig_idx; };
    Resolved resolved[kTableSize];
    uint32_t resolvedCount = 0;

    for (uint32_t i = 0; i < kTableSize; ++i) {
        const uint32_t mid = kTable[i].msg_id;
        if (mid >= static_cast<uint32_t>(arena.messages.size())) continue;
        const Message* msg = arena.messages[mid];
        if (!msg) continue;
        for (uint32_t s = 0; s < msg->signalCount; ++s) {
            if (!msg->signals[s]) continue;
            if (msg->signals[s]->name == kTable[i].sig_name) {
                resolved[resolvedCount++] = { mid, s };
                break;
            }
        }
    }

    if (resolvedCount == 0) return;

    // Track previous value per resolved signal for rising-edge detection
    double prevVal[kTableSize] = {};

    for (const ReplayFrame& frame : frames_) {
        for (uint32_t r = 0; r < resolvedCount; ++r) {
            if (frame.messageId != resolved[r].msg_id) continue;
            if (resolved[r].sig_idx >= frame.signals.size()) continue;

            const double cur  = frame.signals[resolved[r].sig_idx];
            const double prev = prevVal[r];
            // Rising edge: was zero (or not yet seen), now non-zero
            if (prev == 0.0 && cur != 0.0)
                faultTimestamps_.push_back(frame.timestamp_s);
            prevVal[r] = cur;
        }
    }
}

// ---------------------------------------------------------------------------
// Status query
// ---------------------------------------------------------------------------

PlaybackStatus Replay_Controller::status() const {
    PlaybackStatus s;
    s.state          = state_;
    s.playheadTime   = playheadTime_;
    s.startTime      = startTime_;
    s.duration       = endTime_ - startTime_;
    s.endOfRecording = endOfRecording_;
    return s;
}

} // namespace io
