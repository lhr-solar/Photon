/*[Δ] the photon heterogenous compute engine*/
#pragma once
#include "../gpu/gpu.hpp"
#include "../gui/gui.hpp"
#include "../network/network.hpp"
#include "../parse/parse.hpp"
#include "../synth/synth.hpp"
#include "include.hpp"
#include "osmLoader.hpp"

#include <chrono>
#include <thread>
#include <atomic>

class Photon{
private:

public:
    Network network;
    Gpu gpu;
    Gui gui;
    Parse parse;
    Synth synth;
    OSMLoader osmLoader;

    std::chrono::time_point<std::chrono::high_resolution_clock> lastTimestamp;
    std::chrono::time_point<std::chrono::high_resolution_clock> tPrevEnd;
    bool paused = false;
    bool prepared = false;
    
    std::atomic<bool> osmLoadInProgress{false};
    std::thread osmLoadThread;

    Photon();
    ~Photon();

    void prepareScene();
    void initThreads();
    void renderLoop();
    void nextFrame();
    void render();
    void draw();
    void prepareFrame();
    void submitFrame();
    void windowResize();
/*end of photon class*/
};
