#pragma once
#include <SDL3/SDL.h>
struct GUI{
    void buildUI();
    void processEvents(SDL_Event* events);
};
