#include "gui.hpp"

static void rebindPages(GUI* gui){
    if (!gui) return;
    if (gui->homePageIndex < gui->pages.entries.size())
        gui->pages.entries[gui->homePageIndex].render = [gui]() { gui->homeWindow(); };
    if (gui->pages.entries.size() > 1)
        gui->pages.entries[1].render = [gui]() { gui->sceneWindow(); };
    if (gui->pages.entries.size() > 2)
        gui->pages.entries[2].render = [gui]() { gui->networkWindow(); };
    if (gui->pages.entries.size() > 3)
        gui->pages.entries[3].render = [gui]() { gui->parse->arena.statusUI(); };
}

extern "C" __attribute__((visibility("default"))) bool photonBuildUI(GUI* gui){
    if (!gui) return false;
    rebindPages(gui);
    gui->buildUI();
    return true;
}
