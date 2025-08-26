/*[Δ] the photon heterogenous compute engine*/
#pragma once
#include "../gpu/gpu.hpp"
#include "../gui/gui.hpp"
#include "../network/network.hpp"
#include "../parse/parse.hpp"
#include "../synth/synth.hpp"

class Photon{
private:

public:
    Gpu gpu;
    Gui gui;
    Network network;
    Parse parse;
    Synth synth;

    Photon();
    ~Photon();

    void prepareScene();
    void initThreads();
    void renderLoop();

/*end of photon class*/
};
