/*─γ─ Photon Heterogeneous Compute Engine*/
#include "photon.hpp"
#include "include.hpp"

#ifdef XCB
int main(){
    Photon photon;
    photon.gpu.initVulkan();
    photon.gui.initWindow();
    photon.prepareScene();
    photon.parse.initKalmanFilter(photon.gpu.vulkanDevice.logicalDevice, photon.gpu.vulkanDevice.physicalDevice, 
                                  photon.gpu.vulkanDevice.computeQueue, photon.gpu.vulkanDevice.queueFamilyIndices.compute);
    photon.parse.initThreads();
    photon.initThreads();
    photon.renderLoop();
    return 0;
}
#endif

#ifdef WIN
int APIENTRY WinMain(_In_ HINSTANCE hinstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR, _In_ int){
    Photon photon;
    photon.gpu.initVulkan();
    photon.gui.initWindow(hinstance);
    photon.prepareScene();
    photon.parse.initKalmanFilter(photon.gpu.vulkanDevice.logicalDevice, photon.gpu.vulkanDevice.physicalDevice, 
                                  photon.gpu.vulkanDevice.computeQueue, photon.gpu.vulkanDevice.queueFamilyIndices.compute);
    photon.parse.initThreads();
    photon.initThreads();
    photon.renderLoop();
    return 0;
}
#endif
