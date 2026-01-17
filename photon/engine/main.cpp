/*─γ─ Photon Heterogeneous Compute Engine*/
#include "photon.hpp"
#include "include.hpp"
#include "dotenv.hpp"




#ifdef XCB
int main(){
#ifdef USE_DOTENV
    load_dotenv();
#endif
    Photon photon;
    photon.gpu.initVulkan();
    photon.gui.initWindow();
    photon.prepareScene();
    photon.initThreads();
    photon.renderLoop();
    return 0;
}
#endif


#ifdef WIN
int APIENTRY WinMain(_In_ HINSTANCE hinstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR, _In_ int){
#ifdef USE_DOTENV
    load_dotenv();
#endif
    Photon photon;
    photon.gpu.initVulkan();
    photon.gui.initWindow(hinstance);
    photon.prepareScene();
    photon.initThreads();
    photon.renderLoop();
    return 0;
}
#endif
