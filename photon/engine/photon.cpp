/*[Δ] the photon heterogenous compute engine*/
#include <thread>
#include <iostream>

#include "photon.hpp"
#include "include.hpp"
#include "../gpu/gpu.hpp"
#include "../gui/gui.hpp"
#include "imgui.h"
#include "assettoCorsa_dbc.hpp"
#include "daybreak_master_dbc.hpp"
#include "vulkan_core.h"

Photon::Photon(){ 
    logs("[+] Constructing Photon"); 
    gui.ui.parseInterface = &parse;
};
Photon::~Photon(){ 
    logs("[!] Destructuring Photon");
    gpu.cleanup();
};

void Photon::prepareScene(){
#ifdef XCB
    gpu.vulkanSwapchain.initSurface(gpu.instance, gui.connection, gui.window, gpu.vulkanDevice.physicalDevice);
#endif
#ifdef WIN
    gpu.vulkanSwapchain.initSurface(gpu.instance, gui.windowInstance, gui.window, gpu.vulkanDevice.physicalDevice);
#endif
    gpu.createSurfaceCommandPool(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.surfaceQueueNodeIndex);
    gpu.vulkanSwapchain.createSwapChain(&gui.width, &gui.height, gui.settings.vsync, gui.settings.fullscreen, 
    gui.settings.transparent, gpu.vulkanDevice.physicalDevice, gpu.vulkanDevice.logicalDevice);
    gpu.createSurfaceCommandBuffers(gpu.vulkanDevice.logicalDevice, gpu.drawCmdBuffers, gpu.vulkanSwapchain.imageCount);
    gpu.createSynchronizationPrimitives(gpu.vulkanDevice.logicalDevice, gpu.drawCmdBuffers);
    gpu.setupRenderPass(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.surfaceFormat);
    gpu.setupFrameBuffer(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.swapChainBuffers, gpu.vulkanSwapchain.imageCount, gui.width, gui.height);
    gpu.setupDescriptors(gpu.vulkanDevice.logicalDevice);
    gpu.preparePipelines(gpu.vulkanDevice.logicalDevice);
    gui.prepareImGui();
    gui.initResources(gpu.vulkanDevice, gpu.renderPass, gpu.descriptorPool, gpu.descriptorSetLayout, gpu.descriptorSet);
    gui.buildCommandBuffers(gpu.vulkanDevice, gpu.renderPass, gpu.descriptorPool, gpu.descriptorSetLayout, gpu.descriptorSet,
           gpu.frameBuffers, gpu.drawCmdBuffers, gpu.currentBuffer);
    prepared = true;
};

void Photon::initThreads(){
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
        if (prepared && !IsIconic(gui.window)) { startFrame(); }
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
        startFrame();
    }
#endif
    vkDeviceWaitIdle(gpu.vulkanDevice.logicalDevice);
}

void Photon::startFrame(){
    auto tStart = std::chrono::high_resolution_clock::now();
    executeFrame();
    auto tEnd = std::chrono::high_resolution_clock::now();
    gpu.frameTime = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    gpu.frameTime /= 1000.0f;
}

void Photon::executeFrame(){
    getFrame();
    if(!prepared) return;
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)gui.width, (float)gui.height);
    io.DeltaTime = gpu.frameTime;
    manageNetwork();
    gui.buildCommandBuffers(gpu.vulkanDevice, gpu.renderPass, gpu.descriptorPool, gpu.descriptorSetLayout, gpu.descriptorSet, 
            gpu.frameBuffers, gpu.drawCmdBuffers, gpu.currentBuffer);
    gpu.submitInfo.commandBufferCount = 1;
    gpu.submitInfo.pCommandBuffers = &gpu.drawCmdBuffers[gpu.currentBuffer];
    VK_CHECK(vkQueueSubmit(gpu.vulkanDevice.graphicsQueue, 1, &gpu.submitInfo, gpu.fences[gpu.currentBuffer]));
    gpu.currentBuffer = (gpu.currentBuffer + 1) % gpu.vulkanSwapchain.imageCount ;
    pushFrame();
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

        if(gui.ui.currentNetwork == "Server"){
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

void Photon::getFrame(){
    vkWaitForFences(gpu.vulkanDevice.logicalDevice, 1, &gpu.fences[gpu.currentBuffer], VK_TRUE, UINT64_MAX);
	VkResult result = gpu.vulkanSwapchain.acquireNextImage(gpu.vulkanDevice.logicalDevice, gpu.semaphores.presentComplete, &gpu.currentBuffer);
    vkResetFences(gpu.vulkanDevice.logicalDevice, 1, &gpu.fences[gpu.currentBuffer]);
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

void Photon::pushFrame(){
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

    for (uint32_t i = 0; i < gpu.frameBuffers.size(); i++) {
		vkDestroyFramebuffer(gpu.vulkanDevice.logicalDevice, gpu.frameBuffers[i], nullptr);
	}
    gpu.setupFrameBuffer(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.swapChainBuffers, gpu.vulkanSwapchain.imageCount, gui.width, gui.height);
    if ((gui.width > 0.0f) && (gui.height > 0.0f)) {
        ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)(gui.width), (float)(gui.height));
	}
    vkFreeCommandBuffers(gpu.vulkanDevice.logicalDevice, gpu.surfaceCommandPool, gpu.drawCmdBuffers.size(), gpu.drawCmdBuffers.data());
    gpu.createSurfaceCommandBuffers(gpu.vulkanDevice.logicalDevice, gpu.drawCmdBuffers, gpu.vulkanSwapchain.imageCount);
    gui.buildCommandBuffers(gpu.vulkanDevice, gpu.renderPass, gpu.descriptorPool, gpu.descriptorSetLayout, gpu.descriptorSet,
            gpu.frameBuffers, gpu.drawCmdBuffers, gpu.currentBuffer);

    gpu.createSynchronizationPrimitives(gpu.vulkanDevice.logicalDevice, gpu.drawCmdBuffers);
    vkDeviceWaitIdle(gpu.vulkanDevice.logicalDevice);
    gui.resized = true;
    prepared = true;
}
