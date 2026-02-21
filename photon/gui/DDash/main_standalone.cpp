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

#ifdef WIN
int APIENTRY WinMain(_In_ HINSTANCE hinstance,
                     _In_opt_ HINSTANCE,
                     _In_ LPSTR,
                     _In_ int)
{
    Photon photon;
    photon.gui.title = "LHR Dashboard";
    photon.gui.ui.dashboardOnly = true;
    photon.gpu.initVulkan();
    photon.gui.initWindow(hinstance);
    photon.prepareScene();
    photon.initThreads();
    photon.renderLoop();
    return 0;
}
#endif

#ifdef XCB
int main() {
    Photon photon;
    photon.gui.title = "LHR Dashboard";
    photon.gui.ui.dashboardOnly = true;
    photon.gpu.initVulkan();
    photon.gui.initWindow();
    photon.prepareScene();
    photon.initThreads();
    photon.renderLoop();
    return 0;
}
#endif
