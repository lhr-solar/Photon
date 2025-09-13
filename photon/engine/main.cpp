/*─γ─ Photon Heterogeneous Compute Engine*/
#include "photon.hpp"

int main(){
    Photon photon;
    photon.gpu.initVulkan();
    photon.gui.initWindow();
    photon.prepareScene();
    photon.initThreads();
    photon.renderLoop();

    return 0;
}
