#include "include.hpp"
#include "photon.hpp"
#include "arena.hpp"
#include <cstdio>
#include <new>
#include <thread>

int main() {
    logs("Starting");
    Photon photon;
    photon.init();
    photon.renderLoop();
    photon.destroy();
    return 0;
}
