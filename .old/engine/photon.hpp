/*[Δ] the photon heterogenous compute engine*/
#pragma once
#include "../gpu/gpu.hpp"
#include "../gui/gui.hpp"
#include "../network/network.hpp"
#include "../parse/parse.hpp"
#include "../synth/synth.hpp"
#include "include.hpp"

#include <chrono>

class Photon{
private:

public:
    Network network;
    Gpu gpu;
    Gui gui;
    Parse parse;
    Synth synth;

    std::chrono::time_point<std::chrono::high_resolution_clock> lastTimestamp;
    std::chrono::time_point<std::chrono::high_resolution_clock> tPrevEnd;
    bool prepared = false;

    Photon();
    ~Photon();

    void prepareScene();
    void initThreads();
    void renderLoop();
    void startFrame();
    void executeFrame();
    void getFrame();
    void pushFrame();
    void windowResize();
    void manageNetwork();
/*end of photon class*/
};
