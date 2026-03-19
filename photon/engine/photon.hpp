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
    void init();
    void renderLoop();
    void destroy();
};
