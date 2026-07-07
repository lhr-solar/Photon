// network_suspender.cpp
// This is the ONLY file in photon/io/ that includes network/network.hpp.
// It is excluded from the DashboardOnly build via the io CMakeLists.txt conditional.
// Requirements: 4.1, 4.10, 7.2

#include "network_suspender.hpp"
#include "../network/network.hpp"

namespace io {

// Suspends live CAN ingest when entering Replay_Mode (Req 4.1).
// Stops the writer thread (halts live frame writes to the Arena) then
// requests a stop on the backend thread so it cannot spawn a new writer
// while replay is active.
void suspendNetwork(Network& network) {
    // Stop the writer thread and clear activeTCPConfig so the backend loop
    // cannot restart it while suspended.
    network.stopWriter();

    // Request stop on the backend thread and wait for it to exit.
    if (network.backendThread.joinable()) {
        network.backendThread.request_stop();
        network.backendThread.join();
    }
}

// Resumes live CAN ingest when returning to Live_Mode (Req 4.10).
// Restarts the backend thread using the same lambda that Network::init() uses.
void resumeNetwork(Network& network) {
    // Guard against a double-resume: if the thread is still running, leave it.
    if (network.backendThread.joinable()) return;

    network.backendThread = std::jthread(
        [&network](std::stop_token stoken) { network.backend(stoken); });
}

} // namespace io
