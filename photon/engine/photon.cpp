/*[Δ] the photon heterogenous compute engine*/
#include <thread>
#include <iostream>

#include "photon.hpp"
#include "include.hpp"
#include "../gpu/gpu.hpp"
#include "../gui/gui.hpp"
#include "imgui.h"
#include "assettoCorsa_dbc.hpp"
#include "bps_dbc.hpp"
#include "contactor_dbc.hpp"
#include "controls_dbc.hpp"
#include "daq_dbc.hpp"
#include "daybreak_master_dbc.hpp"
#include "mppt_dbc.hpp"
#include "prohelion_wavesculptor22_dbc.hpp"

static_assert(assettoCorsa_dbc_size > 0, "Embedded DBC header generation failed");

Photon::Photon(){ 
    logs("[+] Constructing Photon"); 
    gui.ui.parseINTF = &parse;
};
Photon::~Photon(){ 
    logs("[!] Destructuring Photon");
    gpu.vulkanSwapchain.cleanup(gpu.instance, gpu.vulkanDevice.logicalDevice);
    if(gpu.descriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(gpu.vulkanDevice.logicalDevice, gpu.descriptorPool, nullptr);

};

void Photon::prepareScene(){
#ifdef XCB
    gpu.vulkanSwapchain.initSurface(gpu.instance, gui.connection, gui.window, gpu.vulkanDevice.physicalDevice);
#endif
#ifdef WIN
    gpu.vulkanSwapchain.initSurface(gpu.instance, gui.windowInstance, gui.window, gpu.vulkanDevice.physicalDevice);
#endif
    gpu.vulkanSwapchain.createSurfaceCommandPool(gpu.vulkanDevice.logicalDevice);
    gpu.vulkanSwapchain.createSwapChain(&gui.width, &gui.height, gui.settings.vsync, gui.settings.fullscreen, 
    gui.settings.transparent, gpu.vulkanDevice.physicalDevice, gpu.vulkanDevice.logicalDevice);
    gpu.vulkanSwapchain.createSurfaceCommandBuffers(gpu.vulkanDevice.logicalDevice);
    gpu.createSynchronizationPrimitives(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.drawCmdBuffers);
    gpu.setupRenderPass(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.surfaceFormat);
    gpu.setupDepthStencil(gui.width, gui.height);
    gpu.setupFrameBuffer(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.buffers, gpu.vulkanSwapchain.imageCount, gui.width, gui.height);
    gpu.setupDescriptors(gpu.vulkanDevice.logicalDevice);
    gpu.preparePipelines(gpu.vulkanDevice.logicalDevice);
    gui.prepareImGui();
    gui.initResources(gpu.vulkanDevice, gpu.renderPass, gpu.descriptorPool, gpu.descriptorSetLayout, gpu.descriptorSet);
    gui.buildCommandBuffers(gpu.vulkanDevice, gpu.renderPass, gpu.descriptorPool, gpu.descriptorSetLayout, gpu.descriptorSet,
           gpu.frameBuffers, gpu.vulkanSwapchain.drawCmdBuffers, gpu.frameIndex);
    prepared = true;
};

void Photon::initThreads(){
    //parse.canStore.loadStateFromHeader(daybreak_master_dbc, daybreak_master_dbc_size);
    //std::thread tcp_t(&Network::tcpReader, &network);
    //tcp_t.detach();
    //std::thread parser_t(&Parse::parser, &parse, std::ref(network.tcpQueue));
    //parser_t.detach();

    // this should be controlled by ui.current
    parse.canStore.loadStateFromHeader(assettoCorsa_dbc, assettoCorsa_dbc_size);
    parse.currentDBC = "assettoCorsa";

    network.currentSource_t = std::thread(&Network::corsaReader, &network);
    parse.currentParser_t = std::thread(&Parse::acParser, &parse, std::ref(network.corsaQueue));
}

void Photon::renderLoop(){
    gui.destHeight = gui.height;
    gui.destWidth  = gui.width;
    lastTimestamp = std::chrono::high_resolution_clock::now();
    tPrevEnd = lastTimestamp;
    logs("[Δ] Entering Render Loop");
#ifdef WIN
    MSG msg;
    bool quitMessageReceived = false;
	while (!quitMessageReceived) {
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT) {
				quitMessageReceived = true;
				break;
			}
		}
        if (prepared && !IsIconic(gui.window)) { renderFrame(); }
	}
#endif
#ifdef XCB
    xcb_flush(gui.connection);
    windowResize();
    while (!gui.quit) {
        xcb_generic_event_t *event;
        while((event = xcb_poll_for_event(gui.connection))){
            gui.handleEvent(event); free(event);
        }
        renderFrame();
    }
#endif
    vkDeviceWaitIdle(gpu.vulkanDevice.logicalDevice);
}

void Photon::renderFrame(){
    auto tStart = std::chrono::high_resolution_clock::now();
    prepareFrame();
    auto tEnd = std::chrono::high_resolution_clock::now();
    gpu.frameTime = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    gpu.frameCounter++;
    if(gpu.frameTime < gpu.targetFrameTime){std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(gpu.targetFrameTime - gpu.frameTime))); gpu.frameTime = gpu.targetFrameTime;}
    gpu.frameTime /= 1000.0f;
}

void Photon::prepareFrame(){
    if(!prepared) return;
    //gpu.camera.update(gpu.frameTime); 
    //gpu.updateUniformBuffers(gui.ui.renderSettings.animateLight, gui.ui.renderSettings.lightTimer, gui.ui.renderSettings.lightSpeed);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)gui.width, (float)gui.height);
    io.DeltaTime = gpu.frameTime;
    executeFrame();
}

void Photon::manageNetwork(){
    if(gui.ui.currentDBC != parse.currentDBC){
        std::cout << "does not match!" << std::endl;
        if(gui.ui.currentDBC == "assettoCorsa"){
            parse.canStore = {};
            parse.canStore.loadStateFromHeader(assettoCorsa_dbc, assettoCorsa_dbc_size);
            parse.currentDBC = "assettoCorsa";
        }
        if(gui.ui.currentDBC == "daybreak"){
            parse.canStore = {};
            parse.canStore.loadStateFromHeader(daybreak_master_dbc, daybreak_master_dbc_size);
            parse.currentDBC = "daybreak";
        }
    }
    if(network.currentBackend != gui.ui.currentNetwork){
        network.running = false;
        parse.running = false;
        parse.currentParser_t.join();
        network.currentSource_t.join();

        network.currentBackend = gui.ui.currentNetwork;
        network.running = true;
        parse.running = true;

        if(gui.ui.currentNetwork == "TCP"){
            network.currentSource_t = std::thread(&Network::tcpReader, &network);
            parse.currentParser_t = std::thread(&Parse::parser, &parse, std::ref(network.tcpQueue));
        }
        if(gui.ui.currentNetwork == "Serial"){
            network.currentSource_t = std::thread(&Network::serialReader, &network);
            parse.currentParser_t = std::thread(&Parse::parser, &parse, std::ref(network.serialQueue));
        }
        if(gui.ui.currentNetwork == "Assetto Corsa"){
            network.currentSource_t = std::thread(&Network::corsaReader, &network);
            parse.currentParser_t = std::thread(&Parse::acParser, &parse, std::ref(network.corsaQueue));
        }
    }
    if(gui.ui.rebuildSerial){
        network.running = false;
        parse.running = false;
        parse.currentParser_t.join();
        network.currentSource_t.join();
        network.running = true;
        parse.running = true;

        network.baudRate = gui.ui.baudRate;
        network.serialPort = gui.ui.serialPort;

        network.currentSource_t = std::thread(&Network::serialReader, &network);
        parse.currentParser_t = std::thread(&Parse::parser, &parse, std::ref(network.serialQueue));

        gui.ui.rebuildSerial = false;
    }
}

void Photon::executeFrame(){
    manageNetwork();
    getFrame();
    if (!prepared) { return; }

    //for (auto& [id, msg] : gui.ui.parseINTF->canStore.canMessages) msg.updateMessage(gui.ui.parseINTF); // consider moving out of ui.build()
    gui.buildCommandBuffers(gpu.vulkanDevice, gpu.renderPass, gpu.descriptorPool, gpu.descriptorSetLayout, gpu.descriptorSet, 
            gpu.frameBuffers, gpu.vulkanSwapchain.drawCmdBuffers, gpu.frameIndex);

    gpu.submitInfo.commandBufferCount = 1;
    gpu.submitInfo.pCommandBuffers = &gpu.vulkanSwapchain.drawCmdBuffers[gpu.currentBuffer];
    VK_CHECK(vkQueueSubmit(gpu.vulkanDevice.graphicsQueue, 1, &gpu.submitInfo, VK_NULL_HANDLE));

    submitFrame();
}

void Photon::getFrame(){
    // Acquire the next image from the swap chain
	VkResult result = gpu.vulkanSwapchain.acquireNextImage(gpu.vulkanDevice.logicalDevice, gpu.semaphores.presentComplete, &gpu.currentBuffer);
	if (result == VK_ERROR_SURFACE_LOST_KHR) {
        logs("[!] Swap chain surface lost; resetting render loop");
        prepared = false;
        return;
    }
	if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
        if (result == VK_ERROR_OUT_OF_DATE_KHR) { windowResize(); }
		return;
	} else { VK_CHECK(result);}
}

void Photon::submitFrame(){
    VkResult result = gpu.vulkanSwapchain.queuePresent(gpu.vulkanDevice.graphicsQueue, gpu.currentBuffer, gpu.semaphores.renderComplete);
    if (result == VK_ERROR_SURFACE_LOST_KHR) {
        logs("[!] Swap chain surface lost during present; resetting rendering loop");
        prepared = false;
        return;
    }
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
    VkResult swapchainResult = gpu.vulkanSwapchain.createSwapChain(&gui.width, &gui.height, gui.settings.vsync, 
            gui.settings.fullscreen, gui.settings.transparent, gpu.vulkanDevice.physicalDevice, gpu.vulkanDevice.logicalDevice);
    if (swapchainResult == VK_ERROR_SURFACE_LOST_KHR) {
        logs("[!] Swap chain recreation skipped because the surface was lost");
        return;
    }
    if (swapchainResult != VK_SUCCESS) {
        logs("[!] Swap chain recreation failed with VkResult " << swapchainResult);
        return;
    }
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
    vkFreeCommandBuffers(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.surfaceCommandPool, gpu.vulkanSwapchain.drawCmdBuffers.size(), gpu.vulkanSwapchain.drawCmdBuffers.data());
    gpu.vulkanSwapchain.createSurfaceCommandBuffers(gpu.vulkanDevice.logicalDevice);
    gui.buildCommandBuffers(gpu.vulkanDevice, gpu.renderPass, gpu.descriptorPool, gpu.descriptorSetLayout, gpu.descriptorSet,
            gpu.frameBuffers, gpu.vulkanSwapchain.drawCmdBuffers, gpu.frameIndex);

    for (auto& fence : gpu.waitFences) { vkDestroyFence(gpu.vulkanDevice.logicalDevice, fence, nullptr); }
    gpu.createSynchronizationPrimitives(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.drawCmdBuffers);
    vkDeviceWaitIdle(gpu.vulkanDevice.logicalDevice);
    if ((gui.width > 0.0f) && (gui.height > 0.0f)) { gpu.camera.updateAspectRatio((float)gui.width / (float)gui.height); }
    gui.resized = true;
    prepared = true;
}
