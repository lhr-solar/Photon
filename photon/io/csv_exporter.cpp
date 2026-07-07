// photon/io/csv_exporter.cpp
// Req 2.1-2.7, 2.9, 6.1, 6.4
// No network/ or synth/ includes (Req 7.1)

#include "csv_exporter.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace io {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// RFC 4180 quoting: wrap field in double-quotes if it contains , " or \n;
/// double any internal " characters.  Fields that need no quoting are returned
/// unchanged.  (Req 2.5)
static std::string quotedField(const std::string& raw) {
    bool needs = false;
    for (char c : raw) {
        if (c == ',' || c == '"' || c == '\n') { needs = true; break; }
    }
    if (!needs) return raw;

    std::string out;
    out.reserve(raw.size() + 2);
    out.push_back('"');
    for (char c : raw) {
        if (c == '"') out.push_back('"'); // double internal quote (RFC 4180)
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

/// Serialize a double with 17 significant digits using std::to_chars.
/// No locale, no dynamic allocation.  (Req 6.1, 6.4)
static std::string serializeDouble(double v) {
    // 32 bytes is enough for any double serialized with 17 sig-figs
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf),
                                   v, std::chars_format::general, 17);
    (void)ec; // errc::value_too_large cannot occur with a 32-byte buffer
    return std::string(buf, ptr);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ExportResult exportArena(const Arena& arena, const std::string& outputPath) {
    // Req 2.9 — no active signals: return error, do not create any file
    if (arena.validIds.empty()) {
        return ExportResult{false, "No active signals in Arena", 0, outputPath};
    }

    // -----------------------------------------------------------------------
    // Build ordered column list.
    // Order: message-ID-ascending, signal-index-ascending (Req 2.2)
    // -----------------------------------------------------------------------
    struct Column {
        uint32_t    msgId;
        uint32_t    sigIdx;
        std::string header; // "<MsgName>.<SigName>"
    };

    // Sort a copy of validIds so we iterate in ascending ID order
    std::vector<uint32_t> sortedIds = arena.validIds;
    std::sort(sortedIds.begin(), sortedIds.end());

    std::vector<Column> columns;
    for (uint32_t id : sortedIds) {
        const Message* msg = arena.messages[id];
        if (!msg) continue;
        for (uint32_t s = 0; s < msg->signalCount; ++s) {
            const Signal* sig = msg->signals[s];
            if (!sig) continue;
            std::string hdr = msg->name + "." + sig->name;
            columns.push_back({id, s, std::move(hdr)});
        }
    }

    // -----------------------------------------------------------------------
    // Open output file in binary mode (prevents \n -> \r\n on Windows)
    // (Req 2.6)
    // -----------------------------------------------------------------------
    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) {
        return ExportResult{false,
            "Cannot open '" + outputPath + "': failed to open for writing",
            0, outputPath};
    }

    // Convenience wrappers that return false when badbit is set (Req 2.6)
    const auto writeStr = [&](const std::string& s) -> bool {
        out.write(s.data(), static_cast<std::streamsize>(s.size()));
        return !out.bad();
    };
    const auto writeCh = [&](char c) -> bool {
        out.put(c);
        return !out.bad();
    };
    // Macro-style helper: return error result if write fails
#define WRITE_OR_ERR(expr) \
    do { if (!(expr)) { out.close(); return ExportResult{false, "Write error (disk full?)", rowsWritten, outputPath}; } } while (false)

    uint32_t rowsWritten = 0;

    // -----------------------------------------------------------------------
    // Header row (Req 2.2, 2.4)
    // -----------------------------------------------------------------------
    WRITE_OR_ERR(writeStr("timestamp_s"));
    for (const auto& col : columns) {
        WRITE_OR_ERR(writeCh(','));
        WRITE_OR_ERR(writeStr(quotedField(col.header)));
    }
    WRITE_OR_ERR(writeCh('\n'));

    // -----------------------------------------------------------------------
    // Read signal and time buffers from Arena.
    // Arena::read / readTime take non-const Arena& in the existing interface,
    // so we cast away const here — we only ever read from the arena.
    // -----------------------------------------------------------------------
    Arena& mutableArena = const_cast<Arena&>(arena);

    // Per-column signal buffer
    struct SigBuf {
        const double* ptr{nullptr};
        uint32_t      count{0};   // number of double elements (not bytes)
    };
    std::vector<SigBuf> sigBufs(columns.size());
    for (size_t c = 0; c < columns.size(); ++c) {
        void*    ptr  = nullptr;
        uint32_t size = 0;
        mutableArena.read(columns[c].msgId, columns[c].sigIdx, &ptr, &size);
        // size returned by read() is in bytes; convert to element count
        sigBufs[c] = {static_cast<const double*>(ptr), size / static_cast<uint32_t>(sizeof(double))};
    }

    // Per-message (unique) time buffer — one per sortedId
    struct TimeBuf {
        uint32_t      msgId{};
        const double* ptr{nullptr};
        uint32_t      count{0};   // number of double elements
    };
    std::vector<TimeBuf> timeBufs;
    timeBufs.reserve(sortedIds.size());
    for (uint32_t id : sortedIds) {
        if (!arena.messages[id]) continue;
        void*    ptr  = nullptr;
        uint32_t size = 0;
        mutableArena.readTime(id, &ptr, &size);
        // size returned by readTime() is in bytes; convert to element count
        timeBufs.push_back({id, static_cast<const double*>(ptr),
                            size / static_cast<uint32_t>(sizeof(double))});
    }

    // Map each column to its time-buffer index (linear scan; column list is small)
    std::vector<int> colTimeBufIdx(columns.size(), -1);
    for (size_t c = 0; c < columns.size(); ++c) {
        for (size_t t = 0; t < timeBufs.size(); ++t) {
            if (timeBufs[t].msgId == columns[c].msgId) {
                colTimeBufIdx[c] = static_cast<int>(t);
                break;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Determine the global tick count: maximum published sample count across
    // all active messages (Req 2.3)
    // -----------------------------------------------------------------------
    uint32_t maxTicks = 0;
    for (uint32_t id : sortedIds) {
        const Message* msg = arena.messages[id];
        if (!msg) continue;
        // signalSize.value is stored in bytes; convert to element count
        uint32_t sz = msg->signalSize.value.load(std::memory_order_relaxed)
                      / static_cast<uint32_t>(sizeof(double));
        if (sz > maxTicks) maxTicks = sz;
    }

    // -----------------------------------------------------------------------
    // Data rows — one per global tick index (Req 2.3, 2.4)
    // Each row: timestamp, then K signal values (empty cell if the message
    // has fewer samples than the current tick index).
    // -----------------------------------------------------------------------
    for (uint32_t tick = 0; tick < maxTicks; ++tick) {
        // Timestamp: use the first column whose message has a sample at this tick
        std::string tsStr;
        for (size_t c = 0; c < columns.size(); ++c) {
            int tbi = colTimeBufIdx[c];
            if (tbi >= 0 && timeBufs[tbi].ptr && tick < timeBufs[tbi].count) {
                tsStr = serializeDouble(timeBufs[tbi].ptr[tick]);
                break;
            }
        }
        // If no message has a time entry at this tick, tsStr stays empty —
        // the cell is written as empty but the row is still emitted.

        WRITE_OR_ERR(writeStr(tsStr));

        for (size_t c = 0; c < columns.size(); ++c) {
            WRITE_OR_ERR(writeCh(','));

            const SigBuf& sb = sigBufs[c];
            if (sb.ptr && tick < sb.count) {
                WRITE_OR_ERR(writeStr(quotedField(serializeDouble(sb.ptr[tick]))));
            }
            // else: empty cell (Req 2.3)
        }

        WRITE_OR_ERR(writeCh('\n'));
        ++rowsWritten;
    }

#undef WRITE_OR_ERR

    // Final badbit check before close (belt-and-suspenders)
    if (out.bad()) {
        out.close();
        return ExportResult{false, "Write error (disk full?)", rowsWritten, outputPath};
    }

    out.close();

    // Req 2.7 — report rowsWritten and outputPath
    return ExportResult{true,
        std::to_string(rowsWritten) + " rows -> " + outputPath,
        rowsWritten,
        outputPath};
}

} // namespace io
