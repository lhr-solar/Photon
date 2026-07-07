// Feature: csv-export-replay
// Unit tests for CSV_Exporter error paths.
//   test_exporter_empty_arena   — Validates: Requirements 2.9
//   test_exporter_write_failure — Validates: Requirements 2.6

#include "csv_exporter.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

// ---------------------------------------------------------------------------
// Minimal check macro — prints a message and exits with non-zero on failure.
// ---------------------------------------------------------------------------
#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__          \
                      << "  CHECK(" #cond ") failed\n";                    \
            return false;                                                   \
        }                                                                   \
    } while (false)

// ---------------------------------------------------------------------------
// test_exporter_empty_arena
//
// An Arena whose validIds vector is empty has no active signals.
// exportArena() must:
//   • return ExportResult with ok == false                    (Req 2.9)
//   • set a non-empty error message                          (Req 2.9)
//   • NOT create any file at the output path                 (Req 2.9)
// ---------------------------------------------------------------------------
static bool test_exporter_empty_arena()
{
    // Default-constructed Arena — validIds is empty, no init() required.
    Arena arena{};

    // Use a temp path that should NOT be created.
    const std::string outPath =
        (std::filesystem::temp_directory_path() / "photon_test_empty_arena.csv")
            .string();

    // Remove any leftover from a previous run.
    std::filesystem::remove(outPath);

    io::ExportResult result = io::exportArena(arena, outPath);

    CHECK(!result.ok);
    CHECK(!result.message.empty());
    CHECK(result.rowsWritten == 0);

    // No file should have been created.
    CHECK(!std::filesystem::exists(outPath));

    std::cout << "[PASS] test_exporter_empty_arena\n";
    return true;
}

// ---------------------------------------------------------------------------
// test_exporter_write_failure
//
// Pass a path that cannot be opened for writing (points into a non-existent
// directory).  exportArena() must:
//   • return ExportResult with ok == false                    (Req 2.6)
//   • set a non-empty error message that identifies the path  (Req 2.6)
//   • leave rowsWritten == 0                                  (Req 2.6)
//
// To exercise this path we need at least one valid signal so the exporter
// reaches the file-open step.  We build the smallest possible Arena: one
// message with one signal, no samples written.
// ---------------------------------------------------------------------------
static bool test_exporter_write_failure()
{
    // -----------------------------------------------------------------------
    // Build a minimal valid Arena: message ID 1, one signal named "Volt".
    // -----------------------------------------------------------------------
    arenaConfig cfg{};
    cfg.arenaSize = MINIMUM_ARENA_SIZE;
    cfg.validIds  = {1u};
    cfg.signalCounts[1] = 1;

    Arena arena{};
    arena.init(cfg);

    // Give the message and signal human-readable names so the header row
    // would be well-formed if the file could be opened.
    if (arena.messages[1]) {
        arena.messages[1]->name = "TestMsg";
        if (arena.messages[1]->signals[0])
            arena.messages[1]->signals[0]->name = "Volt";
    }

    // -----------------------------------------------------------------------
    // Use a path inside a deeply nested directory that does not exist.
    // std::ofstream will fail to open it.
    // -----------------------------------------------------------------------
    const std::string outPath =
        "C:\\nonexistent_photon_test_dir_99999\\subdir\\file.csv";

    io::ExportResult result = io::exportArena(arena, outPath);

    CHECK(!result.ok);
    CHECK(!result.message.empty());
    // The error message should mention the output path.
    CHECK(result.message.find("nonexistent_photon_test_dir") != std::string::npos ||
          result.message.find(outPath)                        != std::string::npos);
    CHECK(result.rowsWritten == 0);

    // Clean up Arena memory.
    arena.destroy();

    std::cout << "[PASS] test_exporter_write_failure\n";
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    bool ok = true;
    ok = test_exporter_empty_arena()   && ok;
    ok = test_exporter_write_failure() && ok;

    if (!ok) {
        std::cerr << "One or more tests FAILED.\n";
        return 1;
    }
    std::cout << "All CSV_Exporter error-path tests passed.\n";
    return 0;
}
