#include "photon.hpp"
#include "imgui.h"
#include "vulkan_core.h"
#include <cstdlib>
#include <chrono>

void Photon::init(){
    gpu.init();                                         logs("Initialized GPU");
    gpu.imguiBackend(&gui.titleBar);                    logs("Initialized ImGui");
    parse.init();                                       logs("Initialized Arena");
    gui.init(&gpu, &network, &parse);                   logs("Initialized GUI");
    gui.pendingDBC = parse.activeDBC;
    network.init(&parse, &gui.guiCommands);             logs("Initialized Network");
    gui.bindNetworkResponses(network.getResponseReader());
    gui.protocolConfig.kind = ProtocolKind::TCP;
    gui.queueStartProtocol();
    update();
}

void Photon::renderLoop(){
    logs("Starting render loop");
    while(running){
        auto startFrame = std::chrono::high_resolution_clock::now();
        uint32_t imgIdx{};
        gpu.startFrame(imgIdx);

        appLogic();

        VkCommandBufferBeginInfo cmdBufferBeginInfo {};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VkCommandBuffer& commandBuffer = gpu.commandBuffers[gpu.frameIndex];
        vkResetCommandBuffer(commandBuffer, 0);
        vkBeginCommandBuffer(commandBuffer, &cmdBufferBeginInfo);

        gui.backgroundShader.render(gpu, gpu.commandBuffers[gpu.frameIndex]);
        gui.sceneModel.render(gpu, gpu.commandBuffers[gpu.frameIndex]);
        gpu.imguiPresentation(imgIdx);
        vkEndCommandBuffer(gpu.commandBuffers[gpu.frameIndex]);

        gpu.submitFrame(imgIdx);

        gpu.frameIndex = (gpu.frameIndex+1)%gpu.swapchainImages.size();
        auto endFrame = std::chrono::high_resolution_clock::now();
        deltaTime = std::chrono::duration<double, std::milli>(endFrame - startFrame).count();
    };
};

void Photon::destroy(){
    if (gpuAsyncDispatches.load(std::memory_order_relaxed) != 0) std::quick_exit(0);
    gui.backgroundShader.destroy();
    gui.sceneModel.destroy();
    gpu.destroy();
    network.destroy();
    parse.destroy();
};

void Photon::handleInput(){
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = deltaTime / 1000;
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

void Photon::appLogic(){
    handleInput();
    gui.buildUI();
};

void Photon::update(){

};
