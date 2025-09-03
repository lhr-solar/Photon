/*[Δ] the photon heterogenous compute engine*/
#include <thread>
#include <iostream>

#include "photon.hpp"
#include "include.hpp"
#include "../gpu/gpu.hpp"
#include "../gui/gui.hpp"

Photon::Photon(){
    log("[+] Constructing Photon");
};


Photon::~Photon(){
    log("[!] Destructuring Photon");
}

void Photon::prepareScene(){
   gpu.vulkanSwapchain.initSurface(gpu.instance, gui.connection, gui.window, gpu.vulkanDevice.physicalDevice);
   gpu.vulkanSwapchain.createCommandPool(gpu.vulkanDevice.logicalDevice);
   gpu.vulkanSwapchain.createSwapChain(&gui.width, &gui.height, gui.settings.vsync, gui.settings.fullscreen, gui.settings.transparent, gpu.vulkanDevice.physicalDevice, gpu.vulkanDevice.logicalDevice);
   gpu.vulkanSwapchain.createCommandBuffers(gpu.vulkanDevice.logicalDevice);
   gpu.createSynchronizationPrimitives(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.drawCmdBuffers);
   gpu.setupDepthStencil(gui.width, gui.height);
   gpu.setupRenderPass(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.surfaceFormat);
   gpu.createPipelineCache(gpu.vulkanDevice.logicalDevice);
   gpu.setupFrameBuffer(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.buffers, gpu.vulkanSwapchain.imageCount, gui.width, gui.height);
   gpu.prepareUniformBuffers();
   gpu.updateUniformBuffers(gui.renderSettings.animateLight, gui.renderSettings.lightTimer, gui.renderSettings.lightSpeed);
   gpu.setupLayoutsAndDescriptors(gpu.vulkanDevice.logicalDevice);
   gpu.preparePipelines(gpu.vulkanDevice.logicalDevice);
   gui.prepareImGui();
   gui.initResources(gpu.vulkanDevice, gpu.renderPass);
   gui.buildCommandBuffers(gpu.vulkanDevice, gpu.renderPass, gpu.frameBuffers);
};

void Photon::initThreads(){
    // display splash screen while we initialize all resources?
    // e.g. if we have a swap chain, we should display a frame to the screen
    // start networking service
    // start parsing service / ECS
    // what does our parsing service look like?
}

const char* box =
    " ┌ γ ─────────────────────┐\n"
    " │            ψ           │\n"
    " │             ╲          │\n"
    " │  ξ ──► λ ──► Δ ──► μ   │\n"
    " │             ╱          │\n"
    " │            π           │\n"
    " └────────────────────────┘\n";
void Photon::renderLoop(){
    std::cout << "Cache line size (destructive) : " << std::hardware_destructive_interference_size << std::endl;
    std::cout << "Cache line size (constructive): " << std::hardware_constructive_interference_size << std::endl;
    std::cout << "Usable Hardware Threads: " << std::thread::hardware_concurrency() << std::endl;
    /* temp */
    while (true) {
        std::cout << box << std::flush;
        std::cout << "\033[7A";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}


