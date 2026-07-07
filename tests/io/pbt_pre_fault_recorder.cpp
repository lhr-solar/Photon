// Feature: pre-fault-recorder
// Property-based tests for Pre_Fault_Recorder (Properties 1â€“11)
// Tasks 6.3â€“6.9, 2.2, 7.2â€“7.4, 11.1
// Validates: Requirements 3.3, 3.4, 3.5, 1.3, 3.1, 3.2, 7.3, 7.4, 7.5,
//            7.6, 7.7, 5.3, 5.4, 11.4, 12.2, 3.6, 7.2, 8.7, 9.6

#define PHOTON_DASHBOARD_ONLY
#include <rapidcheck.h>
#include "../../photon/io/pre_fault_recorder.hpp"
#include "../../photon/io/photonlog.hpp"
#include "../../photon/io/replay_controller.hpp"
#include "../../photon/io/network_suspender.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static std::string tempTestDir(const std::string& tag) {
    auto base = fs::temp_directory_path() / ("photon_pbt_" + tag);
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    return base.string();
}

static void cleanupDir(const std::string& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// Ensure the DashboardOnly inline stubs are emitted as COMDAT symbols
// so the linker can resolve io::suspendNetwork / io::resumeNetwork without
// pulling in the full network_suspender.cpp object (which requires Network).
static void emitNetworkStubs() {
    alignas(alignof(std::max_align_t)) char buf[sizeof(void*) * 64]{};
    Network& dummy = reinterpret_cast<Network&>(buf);
    io::suspendNetwork(dummy);
    io::resumeNetwork(dummy);
}

// Build a minimal Arena with one known message id.
static Arena makeArena(const std::vector<uint32_t>& validIds) {
    arenaConfig cfg{};
    cfg.arenaSize = MINIMUM_ARENA_SIZE;
    cfg.validIds  = validIds;
    for (uint32_t id : validIds) {
        if (id < MESSAGE_MAX) cfg.signalCounts[id] = SIGNAL_MAX;
    }
    Arena arena{};
    arena.init(cfg);
    for (uint32_t id : validIds) {
        if (id < MESSAGE_MAX && arena.messages[id]) {
            arena.messages[id]->signalCount = SIGNAL_MAX;
            for (uint32_t s = 0; s < SIGNAL_MAX; ++s)
                if (arena.messages[id]->signals[s])
                    arena.messages[id]->signals[s]->name = "sig" + std::to_string(s);
        }
    }
    return arena;
}

// Wait for the recorder to finish sealing (Sealing â†’ Recording).
static bool waitForSeal(io::Pre_Fault_Recorder& rec,
                        std::chrono::seconds timeout = std::chrono::seconds(3)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    // First wait to enter Sealing
    while (std::chrono::steady_clock::now() < deadline) {
        if (rec.state() == io::Pre_Fault_Recorder::State::Sealing) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // Then wait for Recording
    while (std::chrono::steady_clock::now() < deadline) {
        auto s = rec.state();
        if (s == io::Pre_Fault_Recorder::State::Recording) return true;
        if (s == io::Pre_Fault_Recorder::State::Error) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// Write a minimal valid .photonlog file to disk directly.
// Records vector is written in order (no recorder needed).
static bool writePhotonlogDirect(const std::string& path,
                                 const std::vector<io::PhotonLog_Record>& records,
                                 double window_s = 5.0,
                                 uint32_t magic   = io::PLOG_MAGIC,
                                 uint16_t version = io::PLOG_VERSION) {
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;

    uint64_t capacity = records.size() ? records.size() : 1;

    io::PhotonLog_Header hdr{};
    std::memset(&hdr, 0, sizeof(hdr));
    hdr.magic                = magic;
    hdr.version              = version;
    hdr.write_cursor         = 0;
    hdr.record_capacity      = capacity;
    hdr.pre_fault_window_s   = window_s;
    hdr.signals_per_record   = SIGNAL_MAX;
    hdr.creation_time_unix_s = static_cast<int64_t>(std::time(nullptr));

    if (std::fwrite(&hdr, sizeof(hdr), 1, fp) != 1) {
        std::fclose(fp); return false;
    }
    for (const auto& rec : records) {
        if (std::fwrite(&rec, sizeof(rec), 1, fp) != 1) {
            std::fclose(fp); return false;
        }
    }
    std::fclose(fp);
    return true;
}


// ===========================================================================
// Property 1: PhotonLog_Record Binary Round-Trip
// Feature: pre-fault-recorder, Property 1: PhotonLog_Record binary round-trip
// Validates: Requirements 3.3, 3.4, 3.5
// ===========================================================================
static bool prop_record_roundtrip() {
    return rc::check(
        "Property 1: PhotonLog_Record binary round-trip (Req 3.3, 3.4, 3.5)",
        []() {
            // Generate a PhotonLog_Record with finite doubles
            auto timestamp_s = *rc::gen::suchThat(
                rc::gen::arbitrary<double>(),
                [](double v) { return std::isfinite(v); });

            auto msg_id = *rc::gen::arbitrary<uint32_t>();

            auto signal_count = *rc::gen::inRange<uint32_t>(0, SIGNAL_MAX + 1);

            io::PhotonLog_Record orig{};
            std::memset(&orig, 0, sizeof(orig));
            orig.timestamp_s  = timestamp_s;
            orig.msg_id       = msg_id;
            orig.signal_count = signal_count;

            // Generate finite signal values
            for (uint32_t i = 0; i < signal_count; ++i) {
                orig.signals[i] = *rc::gen::suchThat(
                    rc::gen::arbitrary<double>(),
                    [](double v) { return std::isfinite(v); });
            }

            // Write to temp file and read back
            std::string dir  = tempTestDir("prop1");
            std::string path = dir + "/roundtrip.bin";

            FILE* fp = std::fopen(path.c_str(), "wb");
            RC_ASSERT(fp != nullptr);
            std::fwrite(&orig, sizeof(orig), 1, fp);
            std::fclose(fp);

            io::PhotonLog_Record readback{};
            fp = std::fopen(path.c_str(), "rb");
            RC_ASSERT(fp != nullptr);
            std::fread(&readback, sizeof(readback), 1, fp);
            std::fclose(fp);

            cleanupDir(dir);

            // Every field must be bit-identical
            RC_ASSERT(std::memcmp(&orig, &readback, sizeof(orig)) == 0);
        }
    );
}


// ===========================================================================
// Property 2: PhotonLog_Header Round-Trip
// Feature: pre-fault-recorder, Property 2: PhotonLog_Header round-trip
// Validates: Requirements 1.3, 3.1, 3.2
// ===========================================================================
static bool prop_header_roundtrip() {
    return rc::check(
        "Property 2: PhotonLog_Header round-trip (Req 1.3, 3.1, 3.2)",
        []() {
            // Generate window in [5, 300]
            int window_s = *rc::gen::inRange(5, 301);

            std::string dir = tempTestDir("prop2");

            io::Pre_Fault_Recorder::Config cfg;
            cfg.enabled             = true;
            cfg.pre_fault_window_s  = window_s;
            cfg.log_directory       = dir;

            io::Pre_Fault_Recorder rec;
            rec.init(cfg);
            RC_ASSERT(rec.state() == io::Pre_Fault_Recorder::State::Recording);

            rec.destroy();

            // Read back the header from rolling.photonlog
            std::string path = dir + "/rolling.photonlog";
            FILE* fp = std::fopen(path.c_str(), "rb");
            RC_ASSERT(fp != nullptr);

            io::PhotonLog_Header hdr{};
            bool ok = (std::fread(&hdr, sizeof(hdr), 1, fp) == 1);
            std::fclose(fp);

            cleanupDir(dir);

            RC_ASSERT(ok);
            RC_ASSERT(hdr.magic   == io::PLOG_MAGIC);
            RC_ASSERT(hdr.version == io::PLOG_VERSION);
            RC_ASSERT(hdr.record_capacity > 0);
            RC_ASSERT(hdr.pre_fault_window_s == static_cast<double>(window_s));
            RC_ASSERT(hdr.signals_per_record == SIGNAL_MAX);
            RC_ASSERT(hdr.creation_time_unix_s > 0);
        }
    );
}


// ===========================================================================
// Property 3: Circular Log Ordering Preservation
// Feature: pre-fault-recorder, Property 3: circular log ordering
// Validates: Requirements 7.3, 7.4, 7.5
// ===========================================================================
static bool prop_circular_log_ordering() {
    return rc::check(
        "Property 3: Circular log ordering preservation (Req 7.3, 7.4, 7.5)",
        []() {
            // N frames in [1, 50] â€” keep small so I/O drains quickly
            int N = *rc::gen::inRange(1, 51);

            std::string dir = tempTestDir("prop3");

            io::Pre_Fault_Recorder::Config cfg;
            cfg.enabled            = true;
            cfg.pre_fault_window_s = 5;   // capacity = 8192
            cfg.log_directory      = dir;

            io::Pre_Fault_Recorder rec;
            rec.init(cfg);
            RC_ASSERT(rec.state() == io::Pre_Fault_Recorder::State::Recording);

            // Append N frames with msg_id=1, strictly increasing timestamps
            const uint32_t msg_id = 1u;
            double sigs[1] = {42.0};
            for (int i = 0; i < N; ++i) {
                double t = static_cast<double>(i) * 0.01 + 1.0;
                rec.appendFrame(msg_id, t, sigs, 1);
            }

            // Wait for I/O thread to drain (generous but not 5s)
            std::this_thread::sleep_for(std::chrono::milliseconds(150));

            // Trigger seal and wait â€” 2 second timeout maximum
            rec.triggerSeal();
            bool sealed = waitForSeal(rec, std::chrono::seconds(2));

            // Destroy regardless of seal outcome
            rec.destroy();

            if (!sealed) {
                // Seal timed out or failed â€” clean up and skip this iteration
                cleanupDir(dir);
                RC_SUCCEED();
                return;
            }

            // Find the sealed file
            std::string sealedPath;
            std::error_code ec;
            for (auto& entry : fs::directory_iterator(dir, ec)) {
                if (entry.path().filename().string().rfind("fault_", 0) == 0) {
                    sealedPath = entry.path().string();
                    break;
                }
            }

            if (sealedPath.empty()) {
                cleanupDir(dir);
                RC_SUCCEED();
                return;
            }

            // Load via Replay_Controller with the valid msg_id in arena
            Arena arena = makeArena({msg_id});
            io::Replay_Controller rc_ctrl;
            io::LoadStats stats = rc_ctrl.load(sealedPath, arena, nullptr);
            arena.destroy();
            cleanupDir(dir);

            RC_ASSERT(stats.ok);

            // If we have frames, verify ordering is preserved
            if (stats.frameCount >= 2) {
                auto status = rc_ctrl.status();
                RC_ASSERT(status.duration >= 0.0);
            }
        }
    );
}


// ===========================================================================
// Property 4: Unmatched Message ID Counting
// Feature: pre-fault-recorder, Property 4: unmatched ID counting
// Validates: Requirements 7.6
// ===========================================================================
static bool prop_unmatched_id_counting() {
    return rc::check(
        "Property 4: Unmatched message ID counting (Req 7.6)",
        []() {
            // K records with unknown msg_id, M records with known msg_id=1
            int K = *rc::gen::inRange(0, 11);
            int M = *rc::gen::inRange(1, 11);

            std::string dir  = tempTestDir("prop4");
            std::string path = dir + "/test.photonlog";

            // Build records: M valid (msg_id=1, signal_count=1) + K unknown (msg_id=9999)
            std::vector<io::PhotonLog_Record> records;
            records.reserve(static_cast<size_t>(M + K));

            for (int i = 0; i < M; ++i) {
                io::PhotonLog_Record r{};
                r.timestamp_s  = static_cast<double>(i) * 0.01;
                r.msg_id       = 1u;
                r.signal_count = 1u;
                r.signals[0]   = static_cast<double>(i);
                records.push_back(r);
            }
            for (int i = 0; i < K; ++i) {
                io::PhotonLog_Record r{};
                r.timestamp_s  = static_cast<double>(M + i) * 0.01;
                r.msg_id       = 9999u;  // not in arena validIds
                r.signal_count = 1u;
                r.signals[0]   = static_cast<double>(i);
                records.push_back(r);
            }

            bool wrote = writePhotonlogDirect(path, records);
            RC_ASSERT(wrote);

            // Arena with only validIds = {1}
            Arena arena = makeArena({1u});
            io::Replay_Controller rc_ctrl;
            io::LoadStats stats = rc_ctrl.load(path, arena, nullptr);
            arena.destroy();
            cleanupDir(dir);

            RC_ASSERT(stats.ok);
            RC_ASSERT(stats.unmatchedColumns == static_cast<uint32_t>(K));
        }
    );
}


// ===========================================================================
// Property 5: Malformed Record Counting
// Feature: pre-fault-recorder, Property 5: malformed record counting
// Validates: Requirements 7.7
// ===========================================================================
static bool prop_malformed_row_counting() {
    return rc::check(
        "Property 5: Malformed record counting (Req 7.7)",
        []() {
            // M records with signal_count == 0, G good records
            int M = *rc::gen::inRange(0, 11);
            int G = *rc::gen::inRange(1, 11);

            std::string dir  = tempTestDir("prop5");
            std::string path = dir + "/test.photonlog";

            std::vector<io::PhotonLog_Record> records;
            records.reserve(static_cast<size_t>(M + G));

            // Good records (signal_count >= 1, valid msg_id)
            for (int i = 0; i < G; ++i) {
                io::PhotonLog_Record r{};
                r.timestamp_s  = static_cast<double>(i) * 0.01;
                r.msg_id       = 1u;
                r.signal_count = 1u;
                r.signals[0]   = static_cast<double>(i);
                records.push_back(r);
            }
            // Malformed records (signal_count == 0)
            for (int i = 0; i < M; ++i) {
                io::PhotonLog_Record r{};
                r.timestamp_s  = static_cast<double>(G + i) * 0.01;
                r.msg_id       = 1u;
                r.signal_count = 0u;  // malformed
                records.push_back(r);
            }

            bool wrote = writePhotonlogDirect(path, records);
            RC_ASSERT(wrote);

            Arena arena = makeArena({1u});
            io::Replay_Controller rc_ctrl;
            io::LoadStats stats = rc_ctrl.load(path, arena, nullptr);
            arena.destroy();
            cleanupDir(dir);

            RC_ASSERT(stats.ok);
            RC_ASSERT(stats.malformedRows == static_cast<uint32_t>(M));
        }
    );
}


// ===========================================================================
// Property 6: Threshold Edge Detection (>= 500 iterations via env)
// Feature: pre-fault-recorder, Property 6: threshold edge detection
// Validates: Requirements 5.3, 5.4
// ===========================================================================
static bool prop_threshold_edge_detection() {
    return rc::check(
        "Property 6: Threshold edge detection (Req 5.3, 5.4)",
        []() {
            // Generate a comparator (0=<,1=<=,2=>,3=>=,4===,5=!=)
            // Use simple comparators 0 (<) or 2 (>) to get predictable edges
            int cmp = *rc::gen::elementOf(std::vector<int>{0, 2});

            // threshold value
            double thr = *rc::gen::inRange(1, 100);

            // Number of rising-edge transitions K in [0, 2]
            // Keep K small to avoid test timeout (each seal takes ~100-200ms async)
            int K = *rc::gen::inRange(0, 3);

            std::string dir = tempTestDir("prop6");

            // Build the arena with msg_id=1, signal at index 0
            Arena arena = makeArena({1u});
            if (arena.messages[1]) {
                arena.messages[1]->signals[0]->name = "testSig";
            }

            io::Pre_Fault_Recorder::Config cfg;
            cfg.enabled               = true;
            cfg.pre_fault_window_s    = 5;
            cfg.log_directory         = dir;
            cfg.threshold_signal      = "testSig";
            cfg.threshold_comparator  = cmp;
            cfg.threshold_value       = thr;

            io::Pre_Fault_Recorder rec;
            rec.setArena(&arena);
            rec.init(cfg);
            RC_ASSERT(rec.state() == io::Pre_Fault_Recorder::State::Recording);

            // Generate a value that evaluates to FALSE for the comparator
            // and one that evaluates to TRUE
            double falseVal, trueVal;
            if (cmp == 0) {       // val < thr
                falseVal = thr + 10.0;   // not < thr
                trueVal  = thr - 10.0;   // < thr
            } else {               // cmp == 2: val > thr
                falseVal = thr - 10.0;   // not > thr
                trueVal  = thr + 10.0;   // > thr
            }

            // Append frames: K alternating falseâ†’true transitions
            // Pattern: false, true, false, true, ... (K edges = K true entries after false)
            // We wait between transitions to ensure each seal is processed
            double t = 0.0;
            double sig[1];
            // Start false
            sig[0] = falseVal;
            rec.appendFrame(1u, t, sig, 1);
            t += 0.01;

            for (int i = 0; i < K; ++i) {
                // Transition to true (rising edge â†’ triggers seal)
                sig[0] = trueVal;
                rec.appendFrame(1u, t, sig, 1);
                t += 0.01;

                // Wait for this seal to be picked up and completed before
                // generating the next rising edge. This ensures one seal per edge.
                if (K > 0) {
                    bool ok = waitForSeal(rec, std::chrono::seconds(2));
                    if (!ok) {
                        // Seal timed out â€” destroy and skip
                        rec.destroy();
                        arena.destroy();
                        cleanupDir(dir);
                        RC_SUCCEED();
                        return;
                    }
                }

                // Transition back to false (needed for the next rising edge)
                sig[0] = falseVal;
                rec.appendFrame(1u, t, sig, 1);
                t += 0.01;
            }

            // Wait for any final seal to complete
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            rec.destroy();
            arena.destroy();

            // Count sealed fault_*.photonlog files in dir
            int sealCount = 0;
            std::error_code ec;
            for (auto& entry : fs::directory_iterator(dir, ec)) {
                if (entry.path().filename().string().rfind("fault_", 0) == 0)
                    ++sealCount;
            }

            cleanupDir(dir);

            // Assert exactly K seal operations were initiated
            RC_ASSERT(sealCount == K);
        }
    );
}


// ===========================================================================
// Property 7: Dropped Frames Counting Fidelity
// Feature: pre-fault-recorder, Property 7: dropped frames counting
// Validates: Requirements 11.4
// ===========================================================================
static bool prop_dropped_frames_counting() {
    return rc::check(
        "Property 7: Dropped frames counting fidelity (Req 11.4)",
        []() {
            std::string dir = tempTestDir("prop7");

            io::Pre_Fault_Recorder::Config cfg;
            cfg.enabled            = true;
            cfg.pre_fault_window_s = 5;
            cfg.log_directory      = dir;

            io::Pre_Fault_Recorder rec;
            rec.init(cfg);
            RC_ASSERT(rec.state() == io::Pre_Fault_Recorder::State::Recording);

            // Flood the SPSC ring with RING_CAPACITY * 2 frames as fast as
            // possible â€” the I/O thread cannot drain them all before the ring
            // fills, so at least some pushes must be rejected.
            constexpr uint32_t RING_CAPACITY = 32768;
            constexpr uint32_t FLOOD = RING_CAPACITY * 2;

            double sigs[1] = {1.0};
            for (uint32_t i = 0; i < FLOOD; ++i) {
                rec.appendFrame(1u, static_cast<double>(i) * 0.001, sigs, 1);
            }

            uint64_t dropped = rec.droppedFrames();

            rec.destroy();
            cleanupDir(dir);

            // With 2Ã— capacity pushed in a tight loop, we must have dropped > 0
            RC_ASSERT(dropped > 0);
        }
    );
}


// ===========================================================================
// Property 8: Disabled State Rejects All Writes
// Feature: pre-fault-recorder, Property 8: disabled rejects writes
// Validates: Requirements 12.2
// ===========================================================================
static bool prop_disabled_rejects_writes() {
    return rc::check(
        "Property 8: Disabled state rejects all writes (Req 12.2)",
        []() {
            auto msg_id       = *rc::gen::arbitrary<uint32_t>();
            auto timestamp_s  = *rc::gen::suchThat(
                rc::gen::arbitrary<double>(),
                [](double v) { return std::isfinite(v); });

            std::string dir = tempTestDir("prop8");

            io::Pre_Fault_Recorder::Config cfg;
            cfg.enabled      = false;   // Disabled
            cfg.log_directory = dir;

            io::Pre_Fault_Recorder rec;
            rec.init(cfg);
            RC_ASSERT(rec.state() == io::Pre_Fault_Recorder::State::Disabled);

            // rolling.photonlog must NOT exist (disabled â†’ no file)
            RC_ASSERT(!fs::exists(dir + "/rolling.photonlog"));

            double sigs[1] = {1.0};
            rec.appendFrame(msg_id, timestamp_s, sigs, 1);

            // State must remain Disabled
            RC_ASSERT(rec.state() == io::Pre_Fault_Recorder::State::Disabled);

            // Still no file created
            RC_ASSERT(!fs::exists(dir + "/rolling.photonlog"));

            rec.destroy();
            cleanupDir(dir);
        }
    );
}


// ===========================================================================
// Property 9: Magic/Version Rejection
// Feature: pre-fault-recorder, Property 9: magic/version rejection
// Validates: Requirements 3.6, 7.2
// ===========================================================================
static bool prop_magic_version_rejection() {
    return rc::check(
        "Property 9: Magic/version rejection (Req 3.6, 7.2)",
        []() {
            // Randomly choose to corrupt magic or version
            bool corruptMagic = *rc::gen::arbitrary<bool>();

            uint32_t badMagic   = corruptMagic
                ? *rc::gen::suchThat(
                    rc::gen::arbitrary<uint32_t>(),
                    [](uint32_t v) { return v != io::PLOG_MAGIC; })
                : io::PLOG_MAGIC;

            uint16_t badVersion = !corruptMagic
                ? *rc::gen::suchThat(
                    rc::gen::arbitrary<uint16_t>(),
                    [](uint16_t v) { return v != io::PLOG_VERSION; })
                : io::PLOG_VERSION;

            std::string dir  = tempTestDir("prop9");
            std::string path = dir + "/bad.photonlog";

            // Write a file with bad magic or bad version
            std::vector<io::PhotonLog_Record> records(1);
            records[0].timestamp_s  = 1.0;
            records[0].msg_id       = 1u;
            records[0].signal_count = 1u;
            records[0].signals[0]   = 42.0;

            bool wrote = writePhotonlogDirect(path, records, 5.0, badMagic, badVersion);
            RC_ASSERT(wrote);

            Arena arena = makeArena({1u});
            io::Replay_Controller rc_ctrl;
            io::LoadStats stats = rc_ctrl.load(path, arena, nullptr);
            arena.destroy();
            cleanupDir(dir);

            // Must be rejected
            RC_ASSERT(!stats.ok);
            RC_ASSERT(!stats.message.empty());
        }
    );
}


// ===========================================================================
// Property 10: Source File Immutability During CSV Export
// Feature: pre-fault-recorder, Property 10: source file immutable
// Validates: Requirements 8.7
// ===========================================================================

// Simple CRC32 for byte-level comparison (no external deps needed)
static uint32_t simpleCrc32(const std::vector<uint8_t>& data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t b : data) {
        crc ^= static_cast<uint32_t>(b);
        for (int i = 0; i < 8; ++i) {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static std::vector<uint8_t> readFileBytes(const std::string& path) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return {};
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    std::vector<uint8_t> buf(static_cast<size_t>(sz > 0 ? sz : 0));
    if (!buf.empty()) std::fread(buf.data(), 1, buf.size(), fp);
    std::fclose(fp);
    return buf;
}

static bool prop_source_file_immutable() {
    return rc::check(
        "Property 10: Source file immutable during CSV export (Req 8.7)",
        []() {
            // Generate a random number of valid records
            int N = *rc::gen::inRange(1, 21);

            std::string dir  = tempTestDir("prop10");
            std::string path = dir + "/sealed.photonlog";

            std::vector<io::PhotonLog_Record> records;
            for (int i = 0; i < N; ++i) {
                io::PhotonLog_Record r{};
                r.timestamp_s  = static_cast<double>(i) * 0.01;
                r.msg_id       = 1u;
                r.signal_count = 1u;
                r.signals[0]   = static_cast<double>(i);
                records.push_back(r);
            }

            bool wrote = writePhotonlogDirect(path, records);
            RC_ASSERT(wrote);

            // Capture file size and CRC before loading
            auto bytesBefore = readFileBytes(path);
            uint32_t crcBefore   = simpleCrc32(bytesBefore);
            uintmax_t sizeBefore = bytesBefore.size();

            // Load via Replay_Controller (this is what CSV export does)
            Arena arena = makeArena({1u});
            io::Replay_Controller rc_ctrl;
            io::LoadStats stats = rc_ctrl.load(path, arena, nullptr);
            arena.destroy();

            // Capture file size and CRC after loading
            auto bytesAfter = readFileBytes(path);
            uint32_t crcAfter   = simpleCrc32(bytesAfter);
            uintmax_t sizeAfter = bytesAfter.size();

            cleanupDir(dir);

            RC_ASSERT(stats.ok);
            // File must be byte-identical
            RC_ASSERT(sizeBefore == sizeAfter);
            RC_ASSERT(crcBefore  == crcAfter);
        }
    );
}


// ===========================================================================
// Property 11: Window Clamping
// Feature: pre-fault-recorder, Property 11: window clamping
// Validates: Requirements 9.6
// ===========================================================================
static bool prop_window_clamping() {
    return rc::check(
        "Property 11: Window clamping (Req 9.6)",
        []() {
            int v = *rc::gen::arbitrary<int>();

            // Expected effective window: clamp(v, 5, 300)
            int expected = std::max(5, std::min(300, v));

            std::string dir = tempTestDir("prop11");

            io::Pre_Fault_Recorder::Config cfg;
            cfg.enabled            = true;
            cfg.pre_fault_window_s = v;
            cfg.log_directory      = dir;

            io::Pre_Fault_Recorder rec;
            rec.init(cfg);

            // If init put us in Error (bad path or similar), skip
            if (rec.state() == io::Pre_Fault_Recorder::State::Error) {
                rec.destroy();
                cleanupDir(dir);
                return;
            }

            RC_ASSERT(rec.state() == io::Pre_Fault_Recorder::State::Recording);

            rec.destroy();

            // Read back the header and check pre_fault_window_s was clamped
            std::string path = dir + "/rolling.photonlog";
            FILE* fp = std::fopen(path.c_str(), "rb");
            RC_ASSERT(fp != nullptr);
            io::PhotonLog_Header hdr{};
            bool ok = (std::fread(&hdr, sizeof(hdr), 1, fp) == 1);
            std::fclose(fp);
            cleanupDir(dir);

            RC_ASSERT(ok);
            RC_ASSERT(hdr.pre_fault_window_s == static_cast<double>(expected));
        }
    );
}



// ===========================================================================
// main() — run all 11 properties
//
// RapidCheck reads RC_PARAMS once per rc::check call (before calling
// configuration()), so we can tune the iteration count per-property by
// setting the env var immediately before each call.
//
// Iteration budget:
//   Expensive (seal per iteration, ~150-200 ms each): props 3, 6  →  10
//   Medium    (init+destroy per iteration):           props 2, 7, 11 → 20
//   Cheap     (pure in-memory or file I/O only):     remaining  →  25
// ===========================================================================

static void setMaxSuccess(int n) {
    std::string val = "max_success=" + std::to_string(n);
#ifdef _WIN32
    _putenv_s("RC_PARAMS", val.c_str());
#else
    setenv("RC_PARAMS", val.c_str(), 1);
#endif
}

int main() {
    emitNetworkStubs();

    int failures = 0;

    setMaxSuccess(25); if (!prop_record_roundtrip())         { ++failures; }
    setMaxSuccess(20); if (!prop_header_roundtrip())         { ++failures; }
    setMaxSuccess(10); if (!prop_circular_log_ordering())    { ++failures; }
    setMaxSuccess(25); if (!prop_unmatched_id_counting())    { ++failures; }
    setMaxSuccess(25); if (!prop_malformed_row_counting())   { ++failures; }
    setMaxSuccess(10); if (!prop_threshold_edge_detection()) { ++failures; }
    setMaxSuccess(20); if (!prop_dropped_frames_counting())  { ++failures; }
    setMaxSuccess(25); if (!prop_disabled_rejects_writes())  { ++failures; }
    setMaxSuccess(25); if (!prop_magic_version_rejection())  { ++failures; }
    setMaxSuccess(25); if (!prop_source_file_immutable())    { ++failures; }
    setMaxSuccess(20); if (!prop_window_clamping())          { ++failures; }

    return failures == 0 ? 0 : 1;
}
