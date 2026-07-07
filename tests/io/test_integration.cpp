// Feature: csv-export-replay
// Integration test: full session round-trip fidelity.
//   test_replay_integration — Validates: Requirements 6.2, 6.3
//
// Property 18: For any Arena snapshot, exporting it to a Session_CSV and then
// loading that CSV back (with the same DBC) shall yield a frame table where:
//   (a) frameCount equals the number of rows written during export, and
//   (b) every signal value in every frame is bit-for-bit identical to the
//       corresponding value read from the Arena at export time.
//
// Task 12.1

// Define PHOTON_DASHBOARD_ONLY so that network_suspender.hpp provides the
// inline no-op stubs instead of declarations for the real network symbols.
// Must be defined before any include that transitively pulls in that header.
#define PHOTON_DASHBOARD_ONLY

#include "../../photon/io/csv_exporter.hpp"
#include "../../photon/io/replay_controller.hpp"
#include "../../photon/io/network_suspender.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// CHECK macro — prints location and returns false on failure
// ---------------------------------------------------------------------------
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__           \
                      << "  CHECK(" #cond ") failed\n";                     \
            return false;                                                    \
        }                                                                    \
    } while (false)

// ---------------------------------------------------------------------------
// Test configuration constants
// ---------------------------------------------------------------------------
// Two messages with different signal counts to exercise multi-message paths.
static constexpr uint32_t MSG_A_ID      = 1u;
static constexpr uint32_t MSG_A_SIGNALS = 3u;   // 3 signals in message A
static constexpr uint32_t MSG_B_ID      = 5u;
static constexpr uint32_t MSG_B_SIGNALS = 4u;   // 4 signals in message B

static constexpr uint32_t NUM_FRAMES    = 300u;  // several hundred synthetic frames
static constexpr double   FRAME_DT      = 0.010; // 10 ms between samples → 3.0 s total

// ---------------------------------------------------------------------------
// buildArena
//
// Constructs an Arena with two messages (MSG_A, MSG_B), names every signal,
// and writes NUM_FRAMES synthetic frames with sinusoidal values so the values
// are non-trivial and distinguishable across frames.
//
// Caller must call arena.destroy() when done.
// ---------------------------------------------------------------------------
static Arena buildArena()
{
    arenaConfig cfg{};
    cfg.arenaSize = MINIMUM_ARENA_SIZE;
    cfg.validIds  = { MSG_A_ID, MSG_B_ID };
    cfg.signalCounts[MSG_A_ID] = MSG_A_SIGNALS;
    cfg.signalCounts[MSG_B_ID] = MSG_B_SIGNALS;

    Arena arena{};
    arena.init(cfg);

    // ----- Name message A -----
    arena.messages[MSG_A_ID]->name        = "BatteryPack";
    arena.messages[MSG_A_ID]->signalCount = MSG_A_SIGNALS;
    const char* sigNamesA[MSG_A_SIGNALS] = { "Voltage", "Current", "Temperature" };
    for (uint32_t s = 0; s < MSG_A_SIGNALS; ++s)
        if (arena.messages[MSG_A_ID]->signals[s])
            arena.messages[MSG_A_ID]->signals[s]->name = sigNamesA[s];

    // ----- Name message B -----
    arena.messages[MSG_B_ID]->name        = "MotorCtrl";
    arena.messages[MSG_B_ID]->signalCount = MSG_B_SIGNALS;
    const char* sigNamesB[MSG_B_SIGNALS] = { "RPM", "Torque", "BusVolt", "PhaseA" };
    for (uint32_t s = 0; s < MSG_B_SIGNALS; ++s)
        if (arena.messages[MSG_B_ID]->signals[s])
            arena.messages[MSG_B_ID]->signals[s]->name = sigNamesB[s];

    // ----- Populate NUM_FRAMES synthetic frames -----
    for (uint32_t i = 0; i < NUM_FRAMES; ++i) {
        double t = static_cast<double>(i) * FRAME_DT;

        // Message A: sinusoidal patterns per signal
        double valsA[MSG_A_SIGNALS];
        valsA[0] = 48.0 + 2.0 * std::sin(2.0 * M_PI * t / 0.5);   // Voltage  ~48 V
        valsA[1] = 10.0 * std::cos(2.0 * M_PI * t / 0.3);          // Current  ±10 A
        valsA[2] = 25.0 + 5.0 * std::sin(2.0 * M_PI * t / 2.0);   // Temperature

        bool okA = arena.appendFrame(MSG_A_ID, t, valsA, MSG_A_SIGNALS);
        (void)okA;

        // Message B: different frequencies to avoid trivial patterns
        double valsB[MSG_B_SIGNALS];
        valsB[0] = 3000.0 + 500.0 * std::sin(2.0 * M_PI * t / 0.4);   // RPM
        valsB[1] = 50.0   * std::cos(2.0 * M_PI * t / 0.7);            // Torque
        valsB[2] = 72.0   + 1.5  * std::sin(2.0 * M_PI * t / 1.1);    // BusVolt
        valsB[3] = 200.0  * std::sin(2.0 * M_PI * t / 0.15);           // PhaseA

        bool okB = arena.appendFrame(MSG_B_ID, t, valsB, MSG_B_SIGNALS);
        (void)okB;
    }

    return arena;
}

// ---------------------------------------------------------------------------
// ArenaSnapshot — captures all signal data and timestamps for comparison
// ---------------------------------------------------------------------------
struct ArenaSnapshot {
    struct MsgData {
        uint32_t                         id;
        uint32_t                         signalCount;
        std::vector<double>              times;
        // signals[s] holds all sample values for signal index s
        std::vector<std::vector<double>> signals;
    };
    std::vector<MsgData> messages;
};

static ArenaSnapshot snapshotArena(Arena& arena)
{
    ArenaSnapshot snap;

    for (uint32_t id : arena.validIds) {
        if (!arena.messages[id]) continue;

        ArenaSnapshot::MsgData md;
        md.id          = id;
        md.signalCount = arena.messages[id]->signalCount;

        // Read timestamps
        void*    tData = nullptr;
        uint32_t tSize = 0;
        arena.readTime(id, &tData, &tSize);
        const double* times = static_cast<const double*>(tData);
        // tSize is in bytes; convert to element count
        uint32_t tCount = tSize / static_cast<uint32_t>(sizeof(double));
        md.times.assign(times, times + tCount);

        // Read each signal
        md.signals.resize(md.signalCount);
        for (uint32_t s = 0; s < md.signalCount; ++s) {
            void*    sData = nullptr;
            uint32_t sSize = 0;
            arena.read(id, s, &sData, &sSize);
            const double* vals = static_cast<const double*>(sData);
            // sSize is in bytes; convert to element count
            uint32_t sCount = sSize / static_cast<uint32_t>(sizeof(double));
            md.signals[s].assign(vals, vals + sCount);
        }

        snap.messages.push_back(std::move(md));
    }
    return snap;
}

// ---------------------------------------------------------------------------
// buildReplayArena
//
// Creates a fresh Arena with the same schema (IDs, signal counts, names) but
// no sample data — ready for the replay load.
// ---------------------------------------------------------------------------
static Arena buildReplayArena()
{
    arenaConfig cfg{};
    cfg.arenaSize = MINIMUM_ARENA_SIZE;
    cfg.validIds  = { MSG_A_ID, MSG_B_ID };
    cfg.signalCounts[MSG_A_ID] = MSG_A_SIGNALS;
    cfg.signalCounts[MSG_B_ID] = MSG_B_SIGNALS;

    Arena arena{};
    arena.init(cfg);

    arena.messages[MSG_A_ID]->name        = "BatteryPack";
    arena.messages[MSG_A_ID]->signalCount = MSG_A_SIGNALS;
    const char* sigNamesA[MSG_A_SIGNALS] = { "Voltage", "Current", "Temperature" };
    for (uint32_t s = 0; s < MSG_A_SIGNALS; ++s)
        if (arena.messages[MSG_A_ID]->signals[s])
            arena.messages[MSG_A_ID]->signals[s]->name = sigNamesA[s];

    arena.messages[MSG_B_ID]->name        = "MotorCtrl";
    arena.messages[MSG_B_ID]->signalCount = MSG_B_SIGNALS;
    const char* sigNamesB[MSG_B_SIGNALS] = { "RPM", "Torque", "BusVolt", "PhaseA" };
    for (uint32_t s = 0; s < MSG_B_SIGNALS; ++s)
        if (arena.messages[MSG_B_ID]->signals[s])
            arena.messages[MSG_B_ID]->signals[s]->name = sigNamesB[s];

    return arena;
}

// ---------------------------------------------------------------------------
// bitIdentical — compare two doubles bit-for-bit via memcmp
// ---------------------------------------------------------------------------
static bool bitIdentical(double a, double b)
{
    return std::memcmp(&a, &b, sizeof(double)) == 0;
}

// ---------------------------------------------------------------------------
// test_replay_integration
//
// Full end-to-end session round-trip:
//   1. Populate Arena with NUM_FRAMES synthetic frames (sinusoidal, non-trivial)
//   2. Snapshot all values BEFORE export for later comparison
//   3. Export to a temp CSV — assert ok=true, rowsWritten==NUM_FRAMES
//   4. Destroy original arena; build fresh arena with same schema
//   5. Load CSV via Replay_Controller — assert ok=true
//   6. Assert LoadStats.frameCount == NUM_FRAMES (Req 6.2)
//   7. play() + tick loop advancing to endTime
//   8. Snapshot the replayed Arena
//   9. Assert every value is bit-for-bit identical to the original (Req 6.3)
//  10. Clean up temp file
// ---------------------------------------------------------------------------
static bool test_replay_integration()
{
    // ------------------------------------------------------------------ 1 --
    // Populate the source arena
    Arena srcArena = buildArena();

    // ------------------------------------------------------------------ 2 --
    // Snapshot all signal values before export so we can compare later
    ArenaSnapshot original = snapshotArena(srcArena);

    // Basic sanity check on snapshot
    CHECK(original.messages.size() == 2u);

    // ------------------------------------------------------------------ 3 --
    // Export to a temp CSV
    namespace fs = std::filesystem;
    const std::string tmpPath =
        (fs::temp_directory_path() / "photon_integration_test.csv").string();

    fs::remove(tmpPath);  // clean up any prior run

    io::ExportResult expResult = io::exportArena(srcArena, tmpPath);
    CHECK(expResult.ok);
    CHECK(expResult.rowsWritten == NUM_FRAMES);
    CHECK(fs::exists(tmpPath));

    srcArena.destroy();

    // ------------------------------------------------------------------ 4 --
    // Fresh arena — same schema, no data yet
    Arena replayArena = buildReplayArena();

    // ------------------------------------------------------------------ 5 --
    // Load the CSV into the Replay_Controller
    io::Replay_Controller rc;
    io::LoadStats stats = rc.load(tmpPath, replayArena, nullptr);

    CHECK(stats.ok);

    // ------------------------------------------------------------------ 6 --
    // Req 6.2: frameCount equals rows written during export.
    //
    // The Replay_Controller emits one ReplayFrame per message per CSV row
    // (design doc §2.3: "one ReplayFrame per CSV row per matched message").
    // With NUM_MESSAGES=2 and NUM_FRAMES rows in the CSV, the total frame
    // count is NUM_FRAMES × NUM_MESSAGES.
    static constexpr uint32_t NUM_MESSAGES = 2u;
    CHECK(stats.frameCount == NUM_FRAMES * NUM_MESSAGES);

    // ------------------------------------------------------------------ 7 --
    // play() + tick loop advancing playhead to endTime
    rc.play();
    CHECK(rc.status().state == io::ReplayState::Playing);

    // Advance using dt=0.016 s ticks (~60 fps) until end-of-recording
    constexpr double TICK_DT   = 0.016;
    constexpr int    MAX_TICKS = 5000;  // safety guard against infinite loop
    int              tickCount = 0;

    while (!rc.status().endOfRecording && tickCount < MAX_TICKS) {
        rc.tick(TICK_DT, replayArena);
        ++tickCount;
    }
    CHECK(rc.status().endOfRecording);

    // ------------------------------------------------------------------ 8 --
    // Snapshot the replayed arena
    ArenaSnapshot replayed = snapshotArena(replayArena);

    // ------------------------------------------------------------------ 9 --
    // Req 6.3: every value must be bit-for-bit identical to the original.
    //
    // The replay arena may have a different ring-buffer cursor position than
    // the original, but the samples visible via read() should include all
    // NUM_FRAMES values (the ring buffer is large enough to hold them all).

    CHECK(replayed.messages.size() == original.messages.size());

    for (const auto& origMsg : original.messages) {
        // Find matching replayed message
        const ArenaSnapshot::MsgData* repMsg = nullptr;
        for (const auto& rm : replayed.messages) {
            if (rm.id == origMsg.id) { repMsg = &rm; break; }
        }
        CHECK(repMsg != nullptr);
        CHECK(repMsg->signalCount == origMsg.signalCount);

        // Sample counts must match
        CHECK(repMsg->times.size() == origMsg.times.size());

        uint32_t N = static_cast<uint32_t>(origMsg.times.size());

        for (uint32_t i = 0; i < N; ++i) {
            // Timestamps must match bit-for-bit
            CHECK(bitIdentical(repMsg->times[i], origMsg.times[i]));

            // Every signal value must match bit-for-bit
            for (uint32_t s = 0; s < origMsg.signalCount; ++s) {
                CHECK(origMsg.signals[s].size() == N);
                CHECK(repMsg->signals[s].size() == N);
                if (!bitIdentical(repMsg->signals[s][i], origMsg.signals[s][i])) {
                    std::cerr << "[FAIL] msg=" << origMsg.id
                              << " sig=" << s << " frame=" << i
                              << " original=" << origMsg.signals[s][i]
                              << " replayed=" << repMsg->signals[s][i] << "\n";
                    return false;
                }
            }
        }
    }

    // ----------------------------------------------------------------- 10 --
    // Clean up
    replayArena.destroy();
    fs::remove(tmpPath);

    std::cout << "[PASS] test_replay_integration"
              << " (" << NUM_FRAMES << " frames, 2 messages, "
              << (MSG_A_SIGNALS + MSG_B_SIGNALS) << " signals total)\n";
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    // Force emission of the PHOTON_DASHBOARD_ONLY inline no-op stubs so that
    // lld-link resolves io::suspendNetwork / io::resumeNetwork from THIS TU
    // rather than pulling in network_suspender.cpp.obj (which requires the
    // real Network::stopWriter / Network::backend symbols).
    // The stubs ignore their argument — this is a pure no-op at runtime.
    // This mirrors the explicit call pattern used in test_replay_controller.cpp.
    {
        alignas(alignof(std::max_align_t)) char networkStorage[1] = {};
        Network& dummy = reinterpret_cast<Network&>(networkStorage);
        io::suspendNetwork(dummy);
        io::resumeNetwork(dummy);
    }

    bool ok = test_replay_integration();

    if (!ok) {
        std::cerr << "test_replay_integration FAILED.\n";
        return 1;
    }
    std::cout << "All integration tests passed.\n";
    return 0;
}
