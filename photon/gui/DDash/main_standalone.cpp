/**
 * Standalone Dashboard-Only Entry Point
 * 
 * Boots the full Photon Vulkan/ImGui renderer but only displays
 * the driver dashboard — no network, scene, synth, etc.
 *
 * Build target: DashboardOnly
 * Usage:  .\winBuild.ps1 dash
 */
#include "../../../photon/engine/photon.hpp"
#include <cstdlib>
#include <string>

namespace {

bool envFlagEnabled(const char* name) {
#ifdef _WIN32
    DWORD requiredLength = GetEnvironmentVariableA(name, nullptr, 0);
    if (requiredLength == 0) {
        return false;
    }

    std::string value(requiredLength, '\0');
    DWORD copiedLength = GetEnvironmentVariableA(name, value.data(), requiredLength);
    if (copiedLength == 0) {
        return false;
    }
    value.resize(copiedLength);
#else
    const char* rawValue = std::getenv(name);
    if (!rawValue) {
        return false;
    }
    std::string value = rawValue;
#endif

    return !value.empty()
        && value != "0"
        && value != "false"
        && value != "FALSE"
        && value != "off"
        && value != "OFF";
}

}

#ifdef WIN
int APIENTRY WinMain(_In_ HINSTANCE hinstance,
                     _In_opt_ HINSTANCE,
                     _In_ LPSTR,
                     _In_ int)
{
    Photon photon;
    photon.gui.title = "LHR Dashboard";
    photon.gui.ui.dashboardOnly = true;
    photon.gui.settings.fullscreen = !envFlagEnabled("PHOTON_DASH_WINDOWED");
    photon.gpu.initVulkan();
    photon.gui.initWindow(hinstance);
    photon.prepareScene();
    if (!envFlagEnabled("PHOTON_FAKE_CAN_FAULTS")) {
        photon.initThreads();
    }
    photon.renderLoop();
    return 0;
}
#endif

#ifdef XCB
int main() {
    Photon photon;
    photon.gui.title = "LHR Dashboard";
    photon.gui.ui.dashboardOnly = true;
    photon.gui.settings.fullscreen = !envFlagEnabled("PHOTON_DASH_WINDOWED");
    photon.gpu.initVulkan();
    photon.gui.initWindow();
    if (!envFlagEnabled("PHOTON_FAKE_CAN_FAULTS")) {
        photon.initThreads();  // Start CAN/DBC threads early (runs in parallel with prepareScene)
    }
    photon.prepareScene();
    photon.renderLoop();
    return 0;
}
#endif
