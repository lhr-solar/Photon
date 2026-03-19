#include "photon.hpp"
#include "imgui.h"
#include "vulkan_core.h"
#include <chrono>

void Photon::init(){
    gpu.init(); logs("Initialized GPU");
    gpu.imguiBackend(); logs("Initialized ImGui");
};

void Photon::renderLoop(){
    logs("Starting render loop");
    while(running){
        auto startFrame = std::chrono::high_resolution_clock::now();
        uint32_t imgIdx{};
        SDL_Event events{};
        while(SDL_PollEvent(&events)) gui.processEvents(&events);
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if(keys[SDL_SCANCODE_ESCAPE]){running = false;}
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = deltaTime / 1000;

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
