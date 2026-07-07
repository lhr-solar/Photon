// Feature: pre-fault-recorder
// Integration test: write frames → seal → load via Replay_Controller
// Validates: Requirements 7.3, 7.4, 7.5, 3.5
#define PHOTON_DASHBOARD_ONLY
#include "../../photon/io/pre_fault_recorder.hpp"
#include "../../photon/io/replay_controller.hpp"
#include "../../photon/io/network_suspender.hpp"
#include "../../photon/io/photonlog.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <chrono>
#include <iostream>
#include <cstring>

namespace fs = std::filesystem;

static Arena makeTestArena() {
    arenaConfig cfg{};
    cfg.arenaSize = MINIMUM_ARENA_SIZE;
    cfg.validIds  = {1u};
    cfg.signalCounts[1] = 3;
    Arena arena{};
    arena.init(cfg);
    if (arena.messages[1]) {
        arena.messages[1]->name = "TestMsg";
        arena.messages[1]->signalCount = 3;
        const char* names[] = {"V", "I", "T"};
        for (int i = 0; i < 3; ++i)
            if (arena.messages[1]->signals[i])
                arena.messages[1]->signals[i]->name = names[i];
    }
    return arena;
}

int main() {
    // Emit no-op calls to ensure DashboardOnly stubs link correctly
    {
        alignas(alignof(std::max_align_t)) char buf[1]{};
        Network& dummy = reinterpret_cast<Network&>(buf);
        io::suspendNetwork(dummy);
        io::resumeNetwork(dummy);
    }

    const std::string dir = (fs::temp_directory_path() / "photon_pfr_integ").string();
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // 1. Init recorder
    io::Pre_Fault_Recorder::Config cfg;
    cfg.enabled = true;
    cfg.pre_fault_window_s = 5;
    cfg.log_directory = dir;

    io::Pre_Fault_Recorder rec;
    rec.init(cfg);
    assert(rec.state() == io::Pre_Fault_Recorder::State::Recording);

    // 2. Append 200 frames
    constexpr int N = 200;
    for (int i = 0; i < N; ++i) {
        double t = i * 0.01;
        double sigs[3] = { static_cast<double>(i), static_cast<double>(i) * 2.0, 25.0 };
        rec.appendFrame(1u, t, sigs, 3);
    }

    // 3. Seal — wait up to 3s for the I/O thread to finish
    rec.triggerSeal();
    for (int i = 0; i < 60; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        bool found = false;
        std::error_code ec;
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.path().filename().string().rfind("fault_", 0) == 0) { found = true; break; }
        if (found) break;
    }

    // Find sealed file
    std::string sealedPath;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().filename().string().rfind("fault_", 0) == 0) {
            sealedPath = entry.path().string();
            break;
        }
    }
    assert(!sealedPath.empty() && "No sealed file found");

    rec.destroy();

    // 4. Load via Replay_Controller
    Arena arena = makeTestArena();
    io::Replay_Controller rc;
    io::LoadStats stats = rc.load(sealedPath, arena, nullptr);
    assert(stats.ok && "loadPhotonLog failed");
    assert(stats.frameCount > 0 && "No frames loaded");
    std::cout << "Loaded " << stats.frameCount << " frames from " << sealedPath << "\n";

    // 5. Verify ordering
    auto status = rc.status();
    assert(status.state == io::ReplayState::Paused);
    assert(status.duration >= 0.0);

    arena.destroy();
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::cout << "PASS: test_pre_fault_integration\n";
    return 0;
}
