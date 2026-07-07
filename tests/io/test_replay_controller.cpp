// Feature: csv-export-replay
// Unit tests for Replay_Controller.
//   Load error paths        — Validates: Requirements 3.1, 3.2, 3.4
//   Playback state machines — Validates: Requirements 4.6, 4.7, 4.9
//   NetworkSuspender no-ops — Validates: Requirements 7.3
//
// Tasks 11.2 (load error paths), 11.3 (state transitions), 11.4 (DashboardOnly no-ops)

// ---- Task 11.4: DashboardOnly NetworkSuspender test -------------------------
// The DashboardOnly no-op stubs live behind #ifdef PHOTON_DASHBOARD_ONLY in
// network_suspender.hpp.  We define the macro here (before any include that
// pulls in the header) so that the test TU sees only the inline no-op stubs,
// never the real network symbols.  This verifies Req 7.3: the stubs compile,
// link, and run without referencing any Network subsystem symbol.
#define PHOTON_DASHBOARD_ONLY

#include "../../photon/io/replay_controller.hpp"
#include "../../photon/io/network_suspender.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

// ---------------------------------------------------------------------------
// Minimal arena builder
// ---------------------------------------------------------------------------
// Creates an Arena with one message (id=1, signalCount=1) so that
// Replay_Controller::load() passes the "DBC loaded" guard.
// ---------------------------------------------------------------------------
static Arena makeTestArena() {
    arenaConfig cfg;
    cfg.arenaSize = MINIMUM_ARENA_SIZE;
    cfg.validIds  = {1u};
    cfg.signalCounts[1] = 1;

    Arena arena{};
    arena.init(cfg);

    // Name the message and signal so CSV column matching works.
    if (arena.messages[1]) {
        arena.messages[1]->name         = "TestMsg";
        arena.messages[1]->signalCount  = 1;
        if (arena.messages[1]->signals[0]) {
            arena.messages[1]->signals[0]->name = "TestSig";
        }
    }
    return arena;
}

// ---------------------------------------------------------------------------
// Write a minimal valid Session_CSV to a temp file.
// Returns the path.  The caller is responsible for deleting it.
// ---------------------------------------------------------------------------
static std::string writeTempCsv(const std::string& content) {
    namespace fs = std::filesystem;
    fs::path p = fs::temp_directory_path() / "rc_unit_test_tmp.csv";
    std::ofstream f(p, std::ios::binary);
    f << content;
    f.close();
    return p.string();
}

// ---------------------------------------------------------------------------
// Helper: build a simple two-row CSV that matches the test arena
// (message "TestMsg", signal "TestSig").
// ---------------------------------------------------------------------------
static std::string makeSimpleCsv() {
    return "timestamp_s,TestMsg.TestSig\n"
           "0.0,1.0\n"
           "1.0,2.0\n"
           "2.0,3.0\n";
}

// ===========================================================================
// Task 11.2 — Load error paths
// ===========================================================================

// Validates: Requirement 3.2
// load() with an empty arena (no DBC) must fail and leave state Idle.
static void test_replay_load_no_dbc() {
    Arena emptyArena{};  // no init() call → validIds is empty

    io::Replay_Controller rc;
    io::LoadStats stats = rc.load("/some/file.csv", emptyArena, nullptr);

    assert(!stats.ok);
    assert(rc.status().state == io::ReplayState::Idle);
    assert(!rc.isLoaded());

    std::printf("PASS: test_replay_load_no_dbc\n");
}

// Validates: Requirement 3.4
// load() with a path that doesn't exist must fail with a meaningful message.
static void test_replay_load_missing_file() {
    Arena arena = makeTestArena();

    io::Replay_Controller rc;
    io::LoadStats stats = rc.load("/no/such/file_xyz_does_not_exist.csv", arena, nullptr);

    assert(!stats.ok);
    assert(rc.status().state == io::ReplayState::Idle);
    assert(!rc.isLoaded());

    arena.destroy();
    std::printf("PASS: test_replay_load_missing_file\n");
}

// Validates: Requirement 3.1
// load() with a CSV whose first column is not timestamp_s must fail.
static void test_replay_load_bad_header() {
    Arena arena = makeTestArena();

    std::string badCsv = "time_ms,TestMsg.TestSig\n0.0,1.0\n";
    std::string path   = writeTempCsv(badCsv);

    io::Replay_Controller rc;
    io::LoadStats stats = rc.load(path, arena, nullptr);

    assert(!stats.ok);
    assert(rc.status().state == io::ReplayState::Idle);
    assert(!rc.isLoaded());

    std::filesystem::remove(path);
    arena.destroy();
    std::printf("PASS: test_replay_load_bad_header\n");
}

// ===========================================================================
// Task 11.3 — Playback state transitions
// ===========================================================================

// Validates: Requirement 4.7
// After a successful load (state = Paused), calling seek() must keep the
// state as Paused and move the playhead to the requested target.
static void test_replay_pause_seeks_no_op() {
    Arena arena = makeTestArena();
    std::string csvPath = writeTempCsv(makeSimpleCsv());

    io::Replay_Controller rc;
    io::LoadStats stats = rc.load(csvPath, arena, nullptr);
    assert(stats.ok);

    // After load: Paused at startTime (0.0)
    assert(rc.status().state == io::ReplayState::Paused);

    // Seek to t=1.0 while Paused
    rc.seek(1.0);

    // State must stay Paused (Req 4.7)
    io::PlaybackStatus s = rc.status();
    assert(s.state == io::ReplayState::Paused);
    // Playhead must be at (or clamped to) the target
    assert(s.playheadTime == 1.0);

    std::filesystem::remove(csvPath);
    arena.destroy();
    std::printf("PASS: test_replay_pause_seeks_no_op\n");
}

// Validates: Requirement 4.6
// While Playing, seek() must complete and leave the state as Playing at the
// new target timestamp.
static void test_replay_play_after_seek_resumes() {
    Arena arena = makeTestArena();
    std::string csvPath = writeTempCsv(makeSimpleCsv());

    io::Replay_Controller rc;
    io::LoadStats stats = rc.load(csvPath, arena, nullptr);
    assert(stats.ok);

    // Start playing (network_ is nullptr — no suspendNetwork call with null ptr)
    rc.play();
    assert(rc.status().state == io::ReplayState::Playing);

    // Seek to t=1.0 while Playing
    rc.seek(1.0);

    // State must remain Playing (Req 4.6)
    io::PlaybackStatus s = rc.status();
    assert(s.state == io::ReplayState::Playing);
    assert(s.playheadTime == 1.0);

    std::filesystem::remove(csvPath);
    arena.destroy();
    std::printf("PASS: test_replay_play_after_seek_resumes\n");
}

// Validates: Requirement 4.9
// When tick() advances the playhead past the last frame, the state must
// transition to Paused and endOfRecording must be true.
static void test_replay_end_of_recording() {
    Arena arena = makeTestArena();
    // CSV has frames at t=0.0, t=1.0, t=2.0  → endTime = 2.0
    std::string csvPath = writeTempCsv(makeSimpleCsv());

    io::Replay_Controller rc;
    io::LoadStats stats = rc.load(csvPath, arena, nullptr);
    assert(stats.ok);
    assert(stats.endTime == 2.0);

    rc.play();
    assert(rc.status().state == io::ReplayState::Playing);

    // Tick past the end of the recording (dt = 10 s >> endTime = 2.0)
    rc.tick(10.0, arena);

    io::PlaybackStatus s = rc.status();
    // Req 4.9: must pause automatically and signal end-of-recording
    assert(s.state          == io::ReplayState::Paused);
    assert(s.endOfRecording == true);

    std::filesystem::remove(csvPath);
    arena.destroy();
    std::printf("PASS: test_replay_end_of_recording\n");
}

// ===========================================================================
// Task 11.4 — DashboardOnly NetworkSuspender no-ops
// ===========================================================================

// Validates: Requirement 7.3
// With PHOTON_DASHBOARD_ONLY defined (done at the top of this file),
// io::suspendNetwork and io::resumeNetwork must:
//   - compile (header-only inline stubs)
//   - link without pulling in any network symbols
//   - run without side effects (call completes, no crash)
//
// We pass a dummy Network& (just stack memory) — the stubs ignore it entirely.
static void test_network_suspender_dashboardonly() {
    // Under PHOTON_DASHBOARD_ONLY, Network is only forward-declared.
    // We need a concrete object to pass by reference.  The stub ignores
    // the argument completely, so a zero-initialised char buffer reinterpreted
    // as Network& is sufficient — the stub will never access its members.
    alignas(alignof(std::max_align_t)) char networkStorage[1] = {};
    Network& dummyNetwork = reinterpret_cast<Network&>(networkStorage);

    // These must return immediately without touching dummyNetwork (Req 7.3).
    io::suspendNetwork(dummyNetwork);
    io::resumeNetwork(dummyNetwork);

    std::printf("PASS: test_network_suspender_dashboardonly\n");
}

// ===========================================================================
// Test runner
// ===========================================================================

int main() {
    // Task 11.2 — load error paths
    test_replay_load_no_dbc();
    test_replay_load_missing_file();
    test_replay_load_bad_header();

    // Task 11.3 — playback state transitions
    test_replay_pause_seeks_no_op();
    test_replay_play_after_seek_resumes();
    test_replay_end_of_recording();

    // Task 11.4 — DashboardOnly NetworkSuspender no-ops
    test_network_suspender_dashboardonly();

    std::printf("All tests passed.\n");
    return 0;
}
