#pragma once

// Forward-declare only in DashboardOnly builds; full builds define the real struct.
struct Network;

namespace io {

// Suspends live CAN ingest. Called when entering Replay_Mode.
// Full build: stops the writer thread and requests a stop on the backend thread.
// DashboardOnly build: no-op stub.
#ifdef PHOTON_DASHBOARD_ONLY
inline void suspendNetwork(Network&) {}
inline void resumeNetwork(Network&)  {}
#else
void suspendNetwork(Network& network);   // implemented in network_suspender.cpp
void resumeNetwork(Network& network);
#endif

} // namespace io
