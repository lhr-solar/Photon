#pragma once
#include <cstdint>
#include "../parse/arena.hpp"  // for SIGNAL_MAX

namespace io {

constexpr uint32_t PLOG_MAGIC   = 0x504C4F47u; // 'PLOG'
constexpr uint16_t PLOG_VERSION = 0x0001u;

#pragma pack(push, 1)

// Header occupies exactly the first 4096 bytes of every .photonlog file.
//
// Field sizes (bytes):
//   magic                4
//   version              2
//   _pad0                2
//   write_cursor         8
//   record_capacity      8
//   pre_fault_window_s   8
//   signals_per_record   4
//   _pad1                4
//   creation_time_unix_s 8
//   ─────────────────── 48  ← total before _reserved
//   _reserved         4048  ← 4096 − 48
//   ─────────────────────
//   Total             4096
struct PhotonLog_Header {
    uint32_t magic;                  // must equal PLOG_MAGIC
    uint16_t version;                // must equal PLOG_VERSION
    uint16_t _pad0;
    uint64_t write_cursor;           // next record index to write (wraps mod record_capacity)
    uint64_t record_capacity;        // total record slots in file
    double   pre_fault_window_s;
    uint32_t signals_per_record;     // always SIGNAL_MAX (32)
    uint32_t _pad1;
    int64_t  creation_time_unix_s;
    uint8_t  _reserved[4096 - 48];  // pad to exactly 4096 bytes
};
static_assert(sizeof(PhotonLog_Header) == 4096,
    "PhotonLog_Header must be exactly 4096 bytes");

// One decoded CAN frame.
//
// Field sizes (bytes):
//   timestamp_s    8
//   msg_id         4
//   signal_count   4
//   signals       256  (32 × 8; unused slots zeroed)
//   ─────────────────
//   Total         272
struct PhotonLog_Record {
    double   timestamp_s;
    uint32_t msg_id;
    uint32_t signal_count;
    double   signals[SIGNAL_MAX];    // 32 × 8 = 256 bytes; unused slots zeroed
};
static_assert(sizeof(PhotonLog_Record) == 272,
    "PhotonLog_Record must be exactly 272 bytes");

#pragma pack(pop)

} // namespace io
