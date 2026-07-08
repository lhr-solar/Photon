#include "pre_fault_recorder.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>

#ifdef _WIN32
#  include <windows.h>   // SetFilePointerEx, SetEndOfFile
#  include <io.h>        // _get_osfhandle, _fileno
#else
#  include <fcntl.h>     // posix_fallocate
#endif

namespace io {

static uint64_t nextPow2(uint64_t v) noexcept {
    if (v == 0) return 1;
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return v + 1;
}

void Pre_Fault_Recorder::init(const Config& cfg) {
    
    if (!cfg.enabled) {
        state_.store(State::Disabled, std::memory_order_relaxed);
        return;
    }

    cfg_ = cfg;

    // Clamp window to [5, 300].
    cfg_.pre_fault_window_s = std::clamp(cfg_.pre_fault_window_s, 5, 300);
    // log dir
    {
        std::error_code ec;
        std::filesystem::create_directories(cfg_.log_directory, ec);
        if (ec) {
            last_error_ = "Failed to create log directory \""
                        + cfg_.log_directory + "\": " + ec.message();
            state_.store(State::Error, std::memory_order_relaxed);
            return;
        }
    }

    log_dir_      = cfg_.log_directory;
    rolling_path_ = log_dir_ + "/rolling.pog";

    // current writing file
    if (!openRollingFile()) {
        // last_error_ already set inside openRollingFile()
        state_.store(State::Error, std::memory_order_relaxed);
        return;
    }

    // our queue and recorder stats
    spsc_ = std::make_unique<Ring>();
    dropped_frames_.store(0, std::memory_order_relaxed);
    stop_io_.store(false, std::memory_order_relaxed);
    seal_requested_.store(false, std::memory_order_relaxed);
    batch_size_           = 0;
    seal_counter_         = 0;


    resolveFaultSignals();

    io_thread_ = std::thread(&Pre_Fault_Recorder::ioLoop, this);

    state_.store(State::Recording, std::memory_order_release);
}

void Pre_Fault_Recorder::destroy() {
    stop_io_.store(true, std::memory_order_release);

    if (io_thread_.joinable())
        io_thread_.join();

    if (fp_) {
        std::fclose(fp_);
        fp_ = nullptr;
    }

    spsc_.reset();
    state_.store(State::Disabled, std::memory_order_relaxed);
}


void Pre_Fault_Recorder::reconfigure(const Config& cfg) {
    const bool window_changed =
        (cfg.pre_fault_window_s != cfg_.pre_fault_window_s);
    const bool dir_changed =
        (cfg.log_directory != cfg_.log_directory);

    // If recording and the window or directory changed, seal any buffered data
    // before applying the new config.
    if ((window_changed || dir_changed) &&
        state_.load(std::memory_order_relaxed) == State::Recording &&
        spsc_ && spsc_->size_approx() > 0)
    {
        triggerSeal();
        // brief yeild to seal io
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Tear down the existing I/O thread / file if we are currently active.
    if (state_.load(std::memory_order_relaxed) != State::Disabled) {
        destroy();
    }
    //new config
    init(cfg);
}
void Pre_Fault_Recorder::appendFrame(uint32_t      msg_id,
                                     double         timestamp_s,
                                     const double*  signals,
                                     uint32_t       signal_count)
{
    if (state_.load(std::memory_order_relaxed) != State::Recording)
        return;

    // Build the record entirely on the stack
    PhotonLog_Record rec{};   // zero-init

    rec.timestamp_s  = timestamp_s;
    rec.msg_id       = msg_id;
    rec.signal_count = std::min(signal_count, static_cast<uint32_t>(SIGNAL_MAX));

    if (rec.signal_count > 0 && signals != nullptr) {
        std::memcpy(rec.signals, signals,
                    rec.signal_count * sizeof(double));
    }
    // Unused slots remain zero from the zero-init above.

    if (!spsc_->push(rec)) {
        dropped_frames_.fetch_add(1, std::memory_order_relaxed);
    }

    // Evaluate fault signals — triggers seal on zero→nonzero edge.
    evalFaultSignals(msg_id, signals, signal_count);
}

void Pre_Fault_Recorder::ioLoop() {
    while (!stop_io_.load(std::memory_order_relaxed)) {

        PhotonLog_Record tmp{};
        while (batch_size_ < BATCH_SIZE && spsc_ && spsc_->pop(tmp)) {
            write_batch_[batch_size_++] = tmp;
        }

        if (batch_size_ > 0) {
            flushBatch();
        }

        if (seal_requested_.exchange(false, std::memory_order_acq_rel)) {
            doSeal();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (spsc_) {
        PhotonLog_Record tmp2{};
        while (batch_size_ < BATCH_SIZE && spsc_->pop(tmp2)) {
            write_batch_[batch_size_++] = tmp2;
        }
        if (batch_size_ > 0) {
            flushBatch();
        }
    }
}

void Pre_Fault_Recorder::flushBatch() {
    if (!fp_ || batch_size_ == 0) return;

    // In bounded mode the file is a circular buffer: seek to the slot position.
    // In unbounded mode the file pointer is already positioned at the end of the
    // last write — just append sequentially without seeking.
    if (!cfg_.unbounded) {
        const uint64_t write_slot  = header_.write_cursor;
        const long     byte_offset = static_cast<long>(
            sizeof(PhotonLog_Header) +
            write_slot * sizeof(PhotonLog_Record));

        if (std::fseek(fp_, byte_offset, SEEK_SET) != 0) {
            last_error_ = "fseek failed before fwrite";
            state_.store(State::Error, std::memory_order_release);
            batch_size_ = 0;
            return;
        }
    }

    const size_t written = std::fwrite(write_batch_,
                                       sizeof(PhotonLog_Record),
                                       batch_size_,
                                       fp_);
    if (written != batch_size_) {
        last_error_ = "fwrite returned short ("
                    + std::to_string(written) + " of "
                    + std::to_string(batch_size_) + " records)";
        state_.store(State::Error, std::memory_order_release);
        batch_size_ = 0;
        return;
    }

    if (cfg_.unbounded) {
        // In unbounded mode write_cursor is the total frame count (no wrap).
        header_.write_cursor += batch_size_;
    } else {
        // In bounded mode wrap within record_capacity.
        header_.write_cursor =
            (header_.write_cursor + batch_size_) % header_.record_capacity;
    }

    // Update write_cursor in the on-disk header.
    if (std::fseek(fp_,
                   static_cast<long>(offsetof(PhotonLog_Header, write_cursor)),
                   SEEK_SET) != 0)
    {
        last_error_ = "fseek to header.write_cursor failed";
        state_.store(State::Error, std::memory_order_release);
        batch_size_ = 0;
        return;
    }

    std::fwrite(&header_.write_cursor, sizeof(uint64_t), 1, fp_);

    // In unbounded mode restore the file pointer to the end so the next
    // fwrite appends correctly.
    if (cfg_.unbounded) {
        std::fseek(fp_, 0, SEEK_END);
    }

    batch_size_ = 0;
}


void Pre_Fault_Recorder::doSeal() {
    // progress state
    state_.store(State::Sealing, std::memory_order_release);

    // Flush remaining if any
    if (batch_size_ > 0) {
        flushBatch();
    }
    if (state_.load(std::memory_order_relaxed) == State::Error) {
        return;  // flushBatch set the error
    }

    // Flush OS buffers and ensure write_cursor is up-to-date on disk
    if (fp_) {
        std::fflush(fp_);

        if (std::fseek(fp_,
                       static_cast<long>(offsetof(PhotonLog_Header, write_cursor)),
                       SEEK_SET) == 0)
        {
            std::fwrite(&header_.write_cursor, sizeof(uint64_t), 1, fp_);
            std::fflush(fp_);
        }
    }

    // fault_YYYYMMDD_HHMMSS.photonlog
    char time_buf[32]{};
    {
        std::time_t now = std::time(nullptr);
        std::tm*    tm  = std::localtime(&now);
        if (tm) {
            std::strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", tm);
        } else {// lwk not needed
            std::snprintf(time_buf, sizeof(time_buf), "%lld",
                          static_cast<long long>(now));
        }
    }

    std::string base_name = log_dir_ + "/fault_" + time_buf;
    std::string sealed_path;

    // If the GUI provided an explicit path (via triggerSealTo), use it directly.
    if (!seal_target_path_.empty()) {
        sealed_path = std::move(seal_target_path_);
        seal_target_path_.clear();
    } else {
        namespace fs = std::filesystem;
        // file name collision
        std::string candidate = base_name + ".pog";
        if (!fs::exists(candidate)) {
            sealed_path  = candidate;
            seal_counter_ = 0;
        } else {
            ++seal_counter_;
            do {
                candidate = base_name + "_" + std::to_string(seal_counter_)
                          + ".pog";
                ++seal_counter_;
            } while (fs::exists(candidate));
            --seal_counter_;  // leave at the value we actually used
            sealed_path = candidate;
        }
    }

    if (fp_) {
        std::fclose(fp_);
        fp_ = nullptr;
    }

    // rename
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::rename(rolling_path_, sealed_path, ec);
        if (ec) {
            std::string fallback = log_dir_ + "/fault_tmp_"
                                 + std::to_string(
#ifdef _WIN32
                                     static_cast<long long>(GetCurrentProcessId())
#else
                                     static_cast<long long>(getpid())
#endif
                                   ) + ".pog";
            fs::rename(rolling_path_, fallback, ec);

            last_error_ = "rename to \"" + sealed_path +
                          "\" failed; kept as \"" + fallback + "\"";
            state_.store(State::Error, std::memory_order_release);
            return;
        }
    }

    // new buffer
    if (!openRollingFile()) {
        // last_error_ set inside openRollingFile
        state_.store(State::Error, std::memory_order_release);
        return;
    }

    // Reset the seal counter for the *next* second (new timestamp baseline).
    seal_counter_ = 0;

    state_.store(State::Recording, std::memory_order_release);
}

// ============================================================================
// resolveFaultSignals()   (called from init/reconfigure — no allocation)
// ============================================================================

void Pre_Fault_Recorder::resolveFaultSignals() noexcept {
    // Hardcoded fault-signal table (priority order per Requirement 5.1):
    //   [0] BPS_Fault   — msg_id=1,   signal="BPS_Fault"
    //   [1] VCU_Fault   — msg_id=24,  signal="VCU_Fault"
    //   [2] MPPT_A_Fault— msg_id=513, signal="MPPT_Fault"
    //   [3] MPPT_B_Fault— msg_id=529, signal="MPPT_Fault"
    //   [4] MPPT_C_Fault— msg_id=545, signal="MPPT_Fault"
    struct Entry { uint32_t msg_id; const char* sig_name; const char* label; };
    static constexpr Entry kTable[FAULT_SIGNAL_COUNT] = {
        {  1u, "BPS_Fault",  "BPS_Fault"  },
        { 24u, "VCU_Fault",  "VCU_Fault"  },
        {513u, "MPPT_Fault", "MPPT_A_Fault"},
        {529u, "MPPT_Fault", "MPPT_B_Fault"},
        {545u, "MPPT_Fault", "MPPT_C_Fault"},
    };

    // Populate table — reset all indices first.
    for (uint32_t i = 0; i < FAULT_SIGNAL_COUNT; ++i) {
        fault_signals_[i].msg_id     = kTable[i].msg_id;
        fault_signals_[i].label      = kTable[i].label;
        fault_signals_[i].sig_idx    = UINT32_MAX;
        fault_signals_[i].prev_state = false;
    }

    if (!arena_) return;

    // Walk the Arena and resolve sig_idx for each entry.
    for (uint32_t i = 0; i < FAULT_SIGNAL_COUNT; ++i) {
        const uint32_t target_id = kTable[i].msg_id;

        // Only look in the specific message for this entry.
        if (target_id >= static_cast<uint32_t>(arena_->messages.size()))
            continue;
        const Message* msg = arena_->messages[target_id];
        if (!msg) continue;

        for (uint32_t s = 0; s < msg->signalCount; ++s) {
            if (!msg->signals[s]) continue;
            if (msg->signals[s]->name == kTable[i].sig_name) {
                fault_signals_[i].sig_idx = s;
                break;
            }
        }
    }
}

// ============================================================================
// evalFaultSignals()   (called from parse thread — zero allocation, noexcept)
// ============================================================================

void Pre_Fault_Recorder::evalFaultSignals(uint32_t      msg_id,
                                           const double* signals,
                                           uint32_t      count) noexcept
{
    if (signals == nullptr) return;

    for (uint32_t i = 0; i < FAULT_SIGNAL_COUNT; ++i) {
        FaultSignal& fs = fault_signals_[i];

        if (fs.msg_id  != msg_id)    continue;  // wrong message
        if (fs.sig_idx == UINT32_MAX) continue;  // not yet resolved
        if (fs.sig_idx >= count)      continue;  // out of range

        const bool cur = (signals[fs.sig_idx] != 0.0);

        // Zero → nonzero rising edge: trigger seal exactly once per edge.
        if (!fs.prev_state && cur)
            triggerSeal();

        fs.prev_state = cur;
    }
}

// ============================================================================
// openRollingFile()   (called from init and doSeal — I/O thread or init thread)
// ============================================================================

bool Pre_Fault_Recorder::openRollingFile() {
    fp_ = std::fopen(rolling_path_.c_str(), "w+b");
    if (!fp_) {
        last_error_ = "Cannot open rolling file \""
                    + rolling_path_ + "\": "
                    + std::strerror(errno);
        return false;
    }

    // ── Compute record capacity ────────────────────────────────────────────
    // In unbounded mode the file grows without limit; we store a sentinel
    // capacity of 0 in the header (the reader uses write_cursor instead).
    // In bounded mode we pre-allocate a fixed circular buffer.
    uint64_t cap = 0;
    if (!cfg_.unbounded) {
        const uint64_t raw_capacity =
            static_cast<uint64_t>(cfg_.pre_fault_window_s) * 1000ULL;
        cap = nextPow2(raw_capacity);
        // Clamp to RING_CAPACITY maximum (design constraint).
        if (cap > RING_CAPACITY) cap = RING_CAPACITY;
        if (cap == 0)            cap = 8192;  // minimum sane value
    }

    // ── Build header ───────────────────────────────────────────────────────
    std::memset(&header_, 0, sizeof(header_));
    header_.magic               = PLOG_MAGIC;
    header_.version             = PLOG_VERSION;
    header_.write_cursor        = 0;
    header_.record_capacity     = cap;  // 0 = unbounded
    header_.pre_fault_window_s  = cfg_.unbounded ? 0.0
                                : static_cast<double>(cfg_.pre_fault_window_s);
    header_.signals_per_record  = SIGNAL_MAX;
    header_.creation_time_unix_s = static_cast<int64_t>(std::time(nullptr));

    // ── Write header ───────────────────────────────────────────────────────
    if (std::fwrite(&header_, sizeof(header_), 1, fp_) != 1) {
        last_error_ = "Failed to write header to \""
                    + rolling_path_ + "\"";
        std::fclose(fp_);
        fp_ = nullptr;
        return false;
    }
    std::fflush(fp_);

    // ── Pre-allocate disk space (bounded mode only) ────────────────────────
    // In unbounded mode we skip pre-allocation — the file grows on demand.
    if (!cfg_.unbounded) {
        const uint64_t total_bytes =
            sizeof(PhotonLog_Header) + cap * sizeof(PhotonLog_Record);

#ifdef _WIN32
        {
            HANDLE h = reinterpret_cast<HANDLE>(
                _get_osfhandle(_fileno(fp_)));
            if (h != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER li;
                li.QuadPart = static_cast<LONGLONG>(total_bytes);
                if (SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) {
                    SetEndOfFile(h);
                    li.QuadPart = static_cast<LONGLONG>(sizeof(PhotonLog_Header));
                    SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
                }
            }
        }
#else
        {
            int fd = fileno(fp_);
            if (fd >= 0) {
                (void)posix_fallocate(fd, 0, static_cast<off_t>(total_bytes));
            }
        }
#endif
    }

    // Ensure file pointer is positioned just after the header.
    std::fseek(fp_, static_cast<long>(sizeof(PhotonLog_Header)), SEEK_SET);

    return true;
}

// ============================================================================
// triggerSeal()   (callable from GUI thread or parse thread)
// ============================================================================

void Pre_Fault_Recorder::triggerSeal() {
    seal_requested_.store(true, std::memory_order_release);
}

void Pre_Fault_Recorder::triggerSealTo(std::string path) {
    seal_target_path_ = std::move(path);
    seal_requested_.store(true, std::memory_order_release);
}

// ============================================================================
// state() / lastError() / droppedFrames()
// ============================================================================

Pre_Fault_Recorder::State Pre_Fault_Recorder::state() const noexcept {
    return state_.load(std::memory_order_acquire);
}

std::string Pre_Fault_Recorder::lastError() const {
    // lastError is only written by the I/O thread (or init on the calling
    // thread before the I/O thread starts), so a simple copy is safe here for
    // diagnostic purposes; no mutex needed for a display-only string.
    return last_error_;
}

uint64_t Pre_Fault_Recorder::droppedFrames() const noexcept {
    return dropped_frames_.load(std::memory_order_relaxed);
}

} // namespace io
