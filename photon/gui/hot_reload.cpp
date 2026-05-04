#include "gui.hpp"

#if defined(_WIN32)
extern "C" __declspec(dllexport) bool photonBuildUI(GUI* gui){
#else
extern "C" __attribute__((visibility("default"))) bool photonBuildUI(GUI* gui){
#endif
    if (!gui) return false;
    gui->buildUI();
    return true;
}
