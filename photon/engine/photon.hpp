#pragma once
#include "include.hpp"
#include "gpu.hpp"
#include "gui.hpp"
#include "network.hpp"
#include "parse.hpp"
#include "synth.hpp"

struct Photon {
    GPU gpu;
    GUI gui;
    bool running = true;
    double deltaTime = 16.67 * 1000;
    SDL_WindowID windowID{};
    void init();
    void renderLoop();
    void destroy();
    void handleEvents();
};
