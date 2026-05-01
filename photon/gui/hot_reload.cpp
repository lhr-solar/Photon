#include "gui.hpp"

extern "C" __attribute__((visibility("default"))) bool photonBuildUI(GUI* gui){
    if (!gui) return false;
    gui->buildUI();
    return true;
}
