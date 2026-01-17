/*[Δ] the photon heterogenous compute engine*/
#pragma once
#include "../gpu/gpu.hpp"
#include "../gui/gui.hpp"
#include "../network/network.hpp"
#include "../parse/parse.hpp"
#include "../synth/synth.hpp"
#include "include.hpp"
#include "osmLoader.hpp"
#include "chunking.hpp"

#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>

class Photon{
private:
    std::atomic<bool> osmDiskReady{false};
    std::mutex osmStatusMtx;
    std::string osmDiskStatus;

    std::unordered_map<ChunkId, size_t, ChunkIdHash> osmChunkToModelIndex;
    std::vector<ChunkId> osmModelIndexToChunkId;

    // On-demand chunk fetch worker (Overpass)
    std::atomic<bool> osmChunkFetchRunning{false};
    std::thread osmChunkFetchThread;
    std::mutex osmChunkFetchMtx;
    std::condition_variable osmChunkFetchCv;
    std::deque<ChunkId> osmChunkFetchQueue;
    std::unordered_set<ChunkId, ChunkIdHash> osmChunkFetchInFlight;
    double osmOriginLat = 30.2849;
    double osmOriginLon = -97.7341;

public:
    Network network;
    Gpu gpu;
    Gui gui;
    Parse parse;
    Synth synth;
    OSMLoader osmLoader;
    ChunkManager chunks;

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
