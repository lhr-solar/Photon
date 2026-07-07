#pragma once
#include <cstdint>
#include <string>
#include "../parse/arena.hpp"

// No includes from network/ or synth/ — satisfies Req 7.1

namespace io {

/// Result returned by exportArena().
/// On success: ok=true, message="<N> rows → <path>", rowsWritten=N, outputPath set.
/// On failure: ok=false, message describes the reason, rowsWritten=0, outputPath set.
struct ExportResult {
    bool        ok{false};
    std::string message;     ///< error string on failure; "N rows → path" on success
    uint32_t    rowsWritten{0};
    std::string outputPath;
};

/// Synchronous CSV export.
/// Reads the Arena snapshot and writes a Session_CSV to outputPath.
/// Returns immediately after the file is closed.
/// Thread-safety: must be called from the GUI thread (same thread that reads Arena).
///
/// Req 2.1 — completes write before returning.
/// Req 7.1 — this header includes no network/ or synth/ headers.
ExportResult exportArena(const Arena& arena, const std::string& outputPath);

} // namespace io
