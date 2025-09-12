/*[Δ] the photon heterogenous compute engine*/
#include <thread>
#include <iostream>

#include "photon.hpp"
#include "include.hpp"
#include "../gpu/gpu.hpp"
#include "../gui/gui.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"

Photon::Photon() { log("[+] Constructing Photon"); };
Photon::~Photon(){ log("[!] Destructuring Photon"); }

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
   gui.buildCommandBuffers(gpu.vulkanDevice, gpu.renderPass, gpu.frameBuffers, gpu.vulkanSwapchain.drawCmdBuffers);
};

void Photon::initThreads(){
    // lowkey consider moving this really early?
    log("[+] Initializing Threads ");
    std::cout << "[?] Cache line size (destructive) : " << std::hardware_destructive_interference_size << std::endl;
    std::cout << "[?] Cache line size (constructive): " << std::hardware_constructive_interference_size << std::endl;
    std::cout << "[?] Usable Hardware Threads: " << std::thread::hardware_concurrency() << std::endl;
}

void Photon::renderLoop(){
    gui.destHeight = gui.height;
    gui.destWidth  = gui.width;
    lastTimestamp = std::chrono::high_resolution_clock::now();
    tPrevEnd = lastTimestamp;
    log("[Δ] Entering Render Loop");
    xcb_flush(gui.connection);
    while (true) {
        auto tStart = std::chrono::high_resolution_clock::now();
        if(gui.viewUpdated){ gui.viewUpdated = false; }
        xcb_generic_event_t *event;
        while((event = xcb_poll_for_event(gui.connection))){
            handleEvent();
            free(event);
        }
        render();
        gpu.frameCounter++;
        auto tEnd = std::chrono::high_resolution_clock::now();
        double frameTime = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
        if(frameTime < gpu.targetFrameTime){std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(gpu.targetFrameTime - frameTime)));}
        auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
        gpu.frameTimer = tDiff / 1000.0f;
        gpu.camera.update(gpu.frameTimer); 
        if (gpu.camera.moving()) { gui.viewUpdated = true; }
        if(!paused){
            gpu.timer += gpu.timerSpeed * gpu.frameTimer;
			if (gpu.timer > 1.0) { gpu.timer -= 1.0f; }
        }
    }
}

void Photon::handleEvent(){

}

void Photon::render(){
    if(!prepared) return;
    gpu.updateUniformBuffers(gui.renderSettings.animateLight, gui.renderSettings.lightTimer, gui.renderSettings.lightSpeed);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)gui.width, (float)gui.height);
    io.DeltaTime = gpu.frameTimer;
    draw();
}

void Photon::draw(){
    prepareFrame();
    gui.buildCommandBuffers(gpu.vulkanDevice, gpu.renderPass, gpu.frameBuffers, gpu.vulkanSwapchain.drawCmdBuffers);
    gpu.submitInfo.commandBufferCount = 1;
    gpu.submitInfo.pCommandBuffers = &gpu.vulkanSwapchain.drawCmdBuffers[gpu.currentBuffer]; // ? what is this
    VK_CHECK(vkQueueSubmit(gpu.vulkanDevice.graphicsQueue, 1, &gpu.submitInfo, VK_NULL_HANDLE)); // this fucker lmao
    submitFrame();
}

void Photon::prepareFrame(){
    // Acquire the next image from the swap chain
	VkResult result = gpu.vulkanSwapchain.acquireNextImage(gpu.vulkanDevice.logicalDevice, gpu.semaphores.presentComplete, &gpu.currentBuffer);
	// Recreate the swapchain if it's no longer compatible with the surface (OUT_OF_DATE)
	// SRS - If no longer optimal (VK_SUBOPTIMAL_KHR), wait until submitFrame() in case number of swapchain images will change on resize
	if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
        if (result == VK_ERROR_OUT_OF_DATE_KHR) { windowResize(); }
		return;
	} else { VK_CHECK(result);}
}

void Photon::submitFrame(){
    VkResult result = gpu.vulkanSwapchain.queuePresent(gpu.vulkanDevice.graphicsQueue, gpu.currentBuffer, gpu.semaphores.renderComplete);
    if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
        windowResize();
		if (result == VK_ERROR_OUT_OF_DATE_KHR) { return; }
	} else { VK_CHECK(result); }
	VK_CHECK(vkQueueWaitIdle(gpu.vulkanDevice.graphicsQueue));
}

void Photon::windowResize(){
    if(!prepared) return;
    prepared = false;
    gui.resized = true;
    vkDeviceWaitIdle(gpu.vulkanDevice.logicalDevice);
    gui.width = gui.destWidth;
	gui.height = gui.destHeight;
    gpu.vulkanSwapchain.createSwapChain(&gui.width, &gui.height, gui.settings.vsync, gui.settings.fullscreen, gui.settings.transparent, gpu.vulkanDevice.physicalDevice, gpu.vulkanDevice.logicalDevice);
    vkDestroyImageView(gpu.vulkanDevice.logicalDevice, gpu.depthStencil.view, nullptr);
    vkDestroyImage(gpu.vulkanDevice.logicalDevice, gpu.depthStencil.image, nullptr);
	vkFreeMemory(gpu.vulkanDevice.logicalDevice, gpu.depthStencil.memory, nullptr);
    gpu.setupDepthStencil(gui.width, gui.height);
    for (uint32_t i = 0; i < gpu.frameBuffers.size(); i++) {
		vkDestroyFramebuffer(gpu.vulkanDevice.logicalDevice, gpu.frameBuffers[i], nullptr);
	}
    gpu.setupFrameBuffer(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.buffers, gpu.vulkanSwapchain.imageCount, gui.width, gui.height);
    if ((gui.width > 0.0f) && (gui.height > 0.0f)) {
        ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)(gui.width), (float)(gui.height));
	}
    vkFreeCommandBuffers(gpu.vulkanDevice.logicalDevice, gpu.commandPool, 1, gpu.vulkanSwapchain.drawCmdBuffers.data());
    gpu.vulkanSwapchain.createCommandBuffers(gpu.vulkanDevice.logicalDevice);
    gui.buildCommandBuffers(gpu.vulkanDevice, gpu.renderPass, gpu.frameBuffers, gpu.vulkanSwapchain.drawCmdBuffers);

    for (auto& fence : gpu.waitFences) { vkDestroyFence(gpu.vulkanDevice.logicalDevice, fence, nullptr); }
    gpu.createSynchronizationPrimitives(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.drawCmdBuffers);
    vkDeviceWaitIdle(gpu.vulkanDevice.logicalDevice);
    if ((gui.width > 0.0f) && (gui.height > 0.0f)) { gpu.camera.updateAspectRatio((float)gui.width / (float)gui.height); }
    gui.resized = true;
    prepared = true;
}

const char* box =
    " ┌ γ ─────────────────────┐\n"
    " │            ψ           │\n"
    " │             ╲          │\n"
    " │  ξ ──► λ ──► Δ ──► μ   │\n"
    " │             ╱          │\n"
    " │            π           │\n"
    " └────────────────────────┘\n";
void headlessRuntime(){
    while (true) {
        std::cout << box << std::flush;
        std::cout << "\033[7A";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
