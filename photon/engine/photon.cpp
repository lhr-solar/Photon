#include "photon.hpp"
#include "vulkan_core.h"

void Photon::init(){
    gpu.init(); logs("Initialized GPU");
    gpu.imguiBackend(); logs("Initialized ImGui");
};

void Photon::renderLoop(){
    logs("Starting render loop");
    while(running){
        uint32_t imgIdx{};
        SDL_Event events{};
        while(SDL_PollEvent(&events)) gui.processEvents(&events);
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if(keys[SDL_SCANCODE_ESCAPE]){running = false;}
        vkWaitForFences(gpu.device, 1, &gpu.fences[gpu.frameIndex], VK_TRUE, UINT64_MAX);
        vkAcquireNextImageKHR(gpu.device, gpu.swapchain, UINT64_MAX, 
                gpu.imageAvailableSemaphores[gpu.frameIndex], VK_NULL_HANDLE, &imgIdx);
        vkResetFences(gpu.device, 1, &gpu.fences[gpu.frameIndex]);

        gpu.imguiPresentation(gpu.frameIndex);

        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = 0,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &gpu.imageAvailableSemaphores[gpu.frameIndex],
            .pWaitDstStageMask = waitStages,
            .commandBufferCount = 1,
            .pCommandBuffers = &gpu.commandBuffers[gpu.frameIndex],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &gpu.renderCompleteSemaphores[imgIdx], 
        };
        vkQueueSubmit(gpu.queue, 1, &submitInfo, gpu.fences[gpu.frameIndex]);
        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = NULL,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &gpu.renderCompleteSemaphores[imgIdx],
            .swapchainCount = 1,
            .pSwapchains = &gpu.swapchain, 
            .pImageIndices = &imgIdx,
            .pResults = NULL
        };
        vkQueuePresentKHR(gpu.queue, &presentInfo);
        gpu.frameIndex = (gpu.frameIndex+1)%gpu.swapchainImages.size();
    };
};

void Photon::destroy(){
    gpu.destroy();
};
