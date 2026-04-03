#pragma once
#include "include.hpp"
#include "gpu.hpp"
#include "gui.hpp"
#include "network.hpp"
#include "parse.hpp"
#include "synth.hpp"

struct Photon {
    GPU gpu{};
    GUI gui{};
    Network network{};
    Parse parse{};
    bool running = true;
    double timestamp{};
    double deltaTime = 16.67 * 1000;
    SDL_WindowID windowID{};
    void init();
    void handleInput();
    void appLogic();
    void renderLoop();
    void destroy();
};
