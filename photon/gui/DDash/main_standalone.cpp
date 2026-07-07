// main_standalone.cpp
// Entry point for the DashboardOnly build target.
// Compiled with PHOTON_DASHBOARD_ONLY defined; the io library uses compile-time
// stubs for network suspend/resume so no network/synth symbols are required at
// run-time on the kart's CM5 (though the engine target does still link them at
// compile time — see design doc §2.5 for discussion).
//
// Requirements: 7.3, 7.4

#include "../../engine/photon.hpp"

int main() {
    logs("Starting (DashboardOnly)");
    Photon photon;
    photon.init();
    photon.renderLoop();
    photon.destroy();
    return 0;
}
