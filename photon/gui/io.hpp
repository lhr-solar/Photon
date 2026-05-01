#pragma once
#include "SDL3/SDL.h"
#include "imgui.h"
struct IO{
    static inline void addModifier(ImGuiIO& io, SDL_Keymod mod);
    static inline int sdlMouseToImgui(Uint8 button);
    static ImGuiKey sdlToImgui(SDL_Scancode scancode);
    static void handleInput(SDL_Event* events);
};
