#include "photon.hpp"
#include "imgui.h"
#include "vulkan_core.h"
#include <chrono>

void Photon::init(){
    gpu.init(); logs("Initialized GPU");
    gpu.imguiBackend(); logs("Initialized ImGui");
    windowID = SDL_GetWindowID(gpu.window);
};

void Photon::handleInput(){
    SDL_Event events{};
    while(SDL_PollEvent(&events)) {
        if (events.type == SDL_EVENT_QUIT) running = false;
        if ((events.type == SDL_EVENT_WINDOW_RESIZED) || (events.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)) 
            gpu.resizeWindow();
        gui.processEvents(&events);
    }
};

void Photon::renderLoop(){
    logs("Starting render loop");
    while(running){
        auto startFrame = std::chrono::high_resolution_clock::now();
        uint32_t imgIdx{};
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = deltaTime / 1000;
        handleInput();
        gpu.startFrame(imgIdx);
        gui.buildUI();
        gpu.imguiPresentation(imgIdx);
        gpu.submitFrame(imgIdx);

        gpu.frameIndex = (gpu.frameIndex+1)%gpu.swapchainImages.size();
        auto endFrame = std::chrono::high_resolution_clock::now();
        deltaTime = std::chrono::duration<double, std::milli>(endFrame - startFrame).count();
    };
};

void Photon::destroy(){
    gpu.destroy();
};
