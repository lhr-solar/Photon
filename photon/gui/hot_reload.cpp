#include "gui.hpp"

#if defined(_WIN32)
#define PHOTON_UI_EXPORT extern "C" __declspec(dllexport)
#else
#define PHOTON_UI_EXPORT extern "C" __attribute__((visibility("default")))
#endif

PHOTON_UI_EXPORT bool photonBuildUI(GUI* gui){
    if (!gui) return false;
    gui->buildUI();
    return true;
}

PHOTON_UI_EXPORT void photonDestroyUI(GUI* gui){
    if (!gui) return;
    gui->destroy();
}
