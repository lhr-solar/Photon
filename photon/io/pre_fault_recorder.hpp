#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include "../parse/arena.hpp"
#include "photonlog.hpp"
#include "spsc_ring.hpp"

namespace io {

class Pre_Fault_Recorder {
public:
    // A single auto-trigger rule: seal when signal crosses threshold.
    // comparator: 0=< 1=<= 2=> 3=>= 4=== 5=!=
    struct AutoTrigger {
        std::string signal_name;
        int         comparator{2};   // default: > (fault when value > threshold)
        double      threshold{0.0};  // default: any non-zero = fault
    };

    struct Config {
        bool        enabled{false};
        int         pre_fault_window_s{30};  // clamped to [5, 300]
        std::string log_directory;           // default: Documents/PhotonLogs
        // Primary (user-configurable) threshold shown in settings panel
        std::string threshold_signal;        // empty = disabled
        int         threshold_comparator{0}; // 0=< 1=<= 2=> 3=>= 4=== 5=!=
        double      threshold_value{0.0};
        // Additional always-on triggers (e.g. VCU/BPS fault signals)
        std::vector<AutoTrigger> autoTriggers;
    };

    enum class State { Disabled, Recording, Sealing, Error };

    // -----------------------------------------------------------------------
    // Lifecycle — called from GUI/engine thread
    // -----------------------------------------------------------------------
    void        init(const Config& cfg);
    void        destroy();
    void        reconfigure(const Config& cfg);  // Settings panel Apply

    // -----------------------------------------------------------------------
    // Hot path — called from parse thread ONLY
    // -----------------------------------------------------------------------
    void        appendFrame(uint32_t msg_id, double timestamp_s,
                            const double* signals, uint32_t signal_count);

    // -----------------------------------------------------------------------
    // Control — called from GUI thread
    // -----------------------------------------------------------------------
    void        triggerSeal();
    void        triggerSealTo(std::string path);  // seal to a specific output path
    State       state()         const noexcept;
    std::string lastError()     const;
    uint64_t    droppedFrames() const noexcept;
    std::string logDirectory()  const { return log_dir_; }
    const Config& getConfig()   const { return cfg_; }

    // Call before init() so fault signal resolution can look up signal names.
    void        setArena(Arena* arena) { arena_ = arena; }

private:
    // I/O thread entry point
    void        ioLoop();

    // Called from I/O thread only
    void        doSeal();
    bool        openRollingFile();
    void        flushBatch();

    // Inline fault-signal evaluation — called from appendFrame (parse thread)
    void        evalFaultSignals(uint32_t msg_id, const double* signals, uint32_t count) noexcept;

    // Resolve all fault signal indices against the Arena schema.
    // Must be called from the same thread as init() before io_thread_ starts.
    void        resolveFaultSignals() noexcept;

    // -----------------------------------------------------------------------
    // Parse-thread state (written only by parse thread)
    // -----------------------------------------------------------------------
    Config      cfg_{};

    // Hardcoded fault-signal descriptors (resolved at init time from Arena).
    // Priority order: BPS_Fault, VCU_Fault, MPPT_A_Fault, MPPT_B_Fault, MPPT_C_Fault.
    struct FaultSignal {
        uint32_t    msg_id{UINT32_MAX};   // CAN message ID; UINT32_MAX = not resolved
        uint32_t    sig_idx{UINT32_MAX};  // signal slot within the message
        bool        prev_state{false};    // edge detection state
        const char* label{nullptr};       // e.g. "BPS_Fault"
    };
    static constexpr uint32_t FAULT_SIGNAL_COUNT = 5;
    FaultSignal fault_signals_[FAULT_SIGNAL_COUNT]{};

    // -----------------------------------------------------------------------
    // Shared — atomic
    // -----------------------------------------------------------------------
    std::atomic<State>    state_{State::Disabled};
    std::atomic<bool>     seal_requested_{false};
    std::atomic<uint64_t> dropped_frames_{0};
    std::atomic<bool>     stop_io_{false};

    // Optional explicit output path for the next seal (empty = auto-generate).
    // Written by GUI thread before seal_requested_ is set (release store),
    // read by I/O thread after seal_requested_ is seen (acquire load).
    std::string           seal_target_path_{};

    // Arena pointer held for fault signal resolution (set during init, read-only after)
    Arena*                arena_{nullptr};

    // -----------------------------------------------------------------------
    // SPSC ring — pushed by parse thread, drained by I/O thread
    // -----------------------------------------------------------------------
    static constexpr uint32_t RING_CAPACITY = 32768;  // 32 K entries ≈ 8.7 MB
    using Ring = SPSC_Ring<PhotonLog_Record, RING_CAPACITY>;
    std::unique_ptr<Ring> spsc_;

    // -----------------------------------------------------------------------
    // I/O thread state (accessed only by I/O thread)
    // -----------------------------------------------------------------------
    FILE*            fp_{nullptr};
    PhotonLog_Header header_{};
    std::string      rolling_path_{};
    std::string      log_dir_{};

    // Write-combining batch — fixed member array, no heap allocation (~17 KB)
    static constexpr uint32_t BATCH_SIZE = 64;
    PhotonLog_Record write_batch_[BATCH_SIZE];
    uint32_t         batch_size_{0};  // I/O thread only

    // Sealed-file name collision counter
    int seal_counter_{0};

    std::thread io_thread_;
    std::string last_error_{};
};

} // namespace io
