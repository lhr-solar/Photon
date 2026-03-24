#include "photon.hpp"
#include "imgui.h"
#include "vulkan_core.h"
#include <chrono>

#include "background_frag_spv.hpp"
#include "background_vert_spv.hpp"
#include "daybreak_glb.hpp"
#include "newCar_glb.hpp"

void Photon::init(){
    gpu.init(); logs("Initialized GPU");
    gpu.imguiBackend(); logs("Initialized ImGui");
    windowID = SDL_GetWindowID(gpu.window);
    gui.bindWindow(gpu.window);
    gpu.enableCustomChrome(&gui.chrome);
    //gui.backgroundShader.init(gpu, (uint32_t*)background_vert_spv, background_vert_spv_size, 
                                   //(uint32_t*)background_frag_spv, background_frag_spv_size);
    gui.carModel.init(gpu, newCar_glb, newCar_glb_size);
    gui.setStyle();
};

void Photon::handleInput(){
    ImGuiIO& io = ImGui::GetIO();
    const bool wantTextInput = io.WantTextInput;
    if (wantTextInput != SDL_TextInputActive(gpu.window)) {
        if (wantTextInput) SDL_StartTextInput(gpu.window);
        else SDL_StopTextInput(gpu.window);
    }

    SDL_Event events{};
    while(SDL_PollEvent(&events)) {
        if (events.type == SDL_EVENT_QUIT) running = false;
        if ((events.type == SDL_EVENT_WINDOW_RESIZED) || (events.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)) 
            gpu.requestResize();
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
        if (imgIdx == UINT32_MAX || gpu.swapchainImages.empty() || gpu.commandBuffers.empty()) {
            auto endFrame = std::chrono::high_resolution_clock::now();
            deltaTime = std::chrono::duration<double, std::milli>(endFrame - startFrame).count();
            continue;
        }
        gui.buildUI();

        VkCommandBufferBeginInfo cmdBufferBeginInfo {};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VkCommandBuffer& commandBuffer = gpu.commandBuffers[gpu.frameIndex];
        vkResetCommandBuffer(commandBuffer, 0);
        vkBeginCommandBuffer(commandBuffer, &cmdBufferBeginInfo);

        //gui.backgroundShader.render(gpu, gpu.commandBuffers[gpu.frameIndex]);
        gui.carModel.render(gpu, gpu.commandBuffers[gpu.frameIndex]);
        gpu.imguiPresentation(imgIdx);
        vkEndCommandBuffer(gpu.commandBuffers[gpu.frameIndex]);

        gpu.submitFrame(imgIdx);

        gpu.frameIndex = (gpu.frameIndex+1)%gpu.swapchainImages.size();
        auto endFrame = std::chrono::high_resolution_clock::now();
        deltaTime = std::chrono::duration<double, std::milli>(endFrame - startFrame).count();
    };
};

void Photon::destroy(){
    //gui.backgroundShader.destroy();
    gui.carModel.destroy();
    gpu.destroy();
};
