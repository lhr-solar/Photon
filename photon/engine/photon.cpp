/*[Δ] the photon heterogenous compute engine*/
#include <thread>
#include <iostream>
#include <cmath>

#include "photon.hpp"
#include "include.hpp"
#include "../gpu/gpu.hpp"
#include "../gui/gui.hpp"
#include "imgui.h"

Photon::Photon(){ 
    logs("[+] Constructing Photon"); 
    gui.ui.networkINTF = &network;
};
Photon::~Photon(){ 
    logs("[!] Destructuring Photon");
    
    // Signal cancellation to OSM loader
    osmLoader.cancel();

    // wait for OSM loading thread
    if (osmLoadThread.joinable()) {
        osmLoadThread.join();
    }
    
    for (auto& model : gpu.osmModels) {
        for (auto& mesh : model.meshes) {
            mesh.vertexBuffer.destroy();
            mesh.indexBuffer.destroy();
        }
    }
    gpu.osmModels.clear();
    
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
   gpu.vulkanSwapchain.createSwapChain(&gui.width, &gui.height, gui.settings.vsync, gui.settings.fullscreen, gui.settings.transparent, gpu.vulkanDevice.physicalDevice, gpu.vulkanDevice.logicalDevice);
   gpu.vulkanSwapchain.createSurfaceCommandBuffers(gpu.vulkanDevice.logicalDevice);
   gpu.createSynchronizationPrimitives(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.drawCmdBuffers);
   gpu.setupDepthStencil(gui.width, gui.height);
   gpu.setupRenderPass(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.surfaceFormat);
   gpu.createPipelineCache(gpu.vulkanDevice.logicalDevice);
   gpu.setupFrameBuffer(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.buffers, gpu.vulkanSwapchain.imageCount, gui.width, gui.height);
   gpu.prepareUniformBuffers();
   gpu.updateUniformBuffers(gui.ui.renderSettings.animateLight, gui.ui.renderSettings.lightTimer, gui.ui.renderSettings.lightSpeed);
   gpu.setupLayoutsAndDescriptors(gpu.vulkanDevice.logicalDevice);
   gpu.preparePipelines(gpu.vulkanDevice.logicalDevice);
   
   // Create default white texture for models without textures
   gpu.createDefaultWhiteTexture();
   
   //gpu.loadGLTFModel("models/untitled.gltf");
   gpu.loadGLTFModel("models/daybreak.gltf");
   gpu.setupMeshDescriptors();
   
   gui.prepareImGui();
   gui.initResources(gpu.vulkanDevice, gpu.renderPass);
   gui.buildCommandBuffers(gpu.vulkanDevice, gpu.renderPass, gpu.frameBuffers, gpu.vulkanSwapchain.drawCmdBuffers, &gpu);
   prepared = true;
};

void Photon::initThreads(){
    // lowkey consider moving this really early?
#ifdef XCB
    logs("[+] Initializing Threads ");
    logs("[?] Cache line size (destructive) : " << std::hardware_destructive_interference_size);
    logs("[?] Cache line size (constructive): " << std::hardware_constructive_interference_size);
    logs("[?] Usable Hardware Threads: " << std::thread::hardware_concurrency());
#endif
    std::thread producer_t(&Network::producer, &network);
    producer_t.detach();
    std::thread parser_t(&Network::parser, &network);
    parser_t.detach();
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
        if (prepared && !IsIconic(gui.window)) { nextFrame(); }
	}
#endif
#ifdef XCB
    xcb_flush(gui.connection);
    windowResize();
    while (!gui.quit) {
        xcb_generic_event_t *event;
        while((event = xcb_poll_for_event(gui.connection))){
            gui.handleEvent(event);
            free(event);
        }
        nextFrame();
    }
#endif
    vkDeviceWaitIdle(gpu.vulkanDevice.logicalDevice);
}

void Photon::nextFrame(){
    auto tStart = std::chrono::high_resolution_clock::now();
	if (gui.viewUpdated){ gui.viewUpdated = false; }
	
	// OSM loaded
	if (gui.ui.renderSettings.osmFetchRequested && !osmLoadInProgress) {
	    gui.ui.renderSettings.osmFetchRequested = false;
	    osmLoadInProgress = true;
	    
	    // join thread
	    if (osmLoadThread.joinable()) {
	        osmLoadThread.join();
	    }
	    
        osmLoader.resetCancel();
	    osmLoadThread = std::thread([this]() {
	        logs("[OSM] Starting fetch in background thread");
	        OSMConfig config;
	        config.centerLat = gui.ui.renderSettings.osmLat;
	        config.centerLon = gui.ui.renderSettings.osmLon;
	        config.radiusMeters = gui.ui.renderSettings.osmRadius;
	        
	        bool success = osmLoader.fetchAndBuild(config);
	        
	        if (success) {
	            for (auto& model : gpu.osmModels) {
	                for (auto& mesh : model.meshes) {
	                    mesh.vertexBuffer.destroy();
	                    mesh.indexBuffer.destroy();
	                }
	            }
	            gpu.osmModels.clear();
	            
	        
	            osmLoader.uploadToGPU(&gpu.vulkanDevice, gpu.osmModels);
	            gpu.osmStatus = osmLoader.getStatus();
	            logs("[+] OSM load complete: " << gpu.osmModels.size() << " models");
	        } else {
	            gpu.osmStatus = "OSM fetch failed: " + osmLoader.getStatus();
	            logs("[!] " << gpu.osmStatus);
	        }	        osmLoadInProgress = false;
	    });
	}
	
    // Update camera to follow car and look at it (car position controlled via ImGui sliders)
    {
        const float followDistance = 10.0f; // meters behind
        const float followHeight   = -10.0f;  // meters above car's roof

        glm::vec3 carPos = gui.ui.renderSettings.modelPosition;
        float yawDeg = gui.ui.renderSettings.modelRotation.y;

        // Build the same rotation stack used for the GLTF draw so our camera basis matches
        glm::mat4 rotMatrix(1.0f);
        rotMatrix = glm::rotate(rotMatrix, glm::radians(yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));
        rotMatrix = glm::rotate(rotMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        // Derive the model-space forward vector after all rotations (Z-forward in GLTF)
        glm::vec3 forward = glm::normalize(glm::vec3(rotMatrix * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

        // Position the camera a fixed distance behind and above the car
        glm::vec3 camPos = carPos - forward * followDistance + worldUp * followHeight;
        gpu.camera.position = camPos;

        // Aim the camera at the car using a stable world-up vector to avoid roll
        gpu.camera.setViewTarget(carPos, worldUp);
    }
	
	render();
    gpu.frameCounter++;
    auto tEnd = std::chrono::high_resolution_clock::now();
    double frameTime = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    if(frameTime < gpu.targetFrameTime){std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(gpu.targetFrameTime - frameTime))); frameTime = gpu.targetFrameTime;}
    auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    gpu.frameTimer = frameTime / 1000.0f;
    
    if (gpu.camera.moving()) { gui.viewUpdated = true; }
    if(!paused){
        gpu.timer += gpu.timerSpeed * gpu.frameTimer;
        if (gpu.timer > 1.0) { gpu.timer -= 1.0f; }
    }
}

void Photon::render(){
    if(!prepared) return;
    gpu.updateUniformBuffers(gui.ui.renderSettings.animateLight, gui.ui.renderSettings.lightTimer, gui.ui.renderSettings.lightSpeed);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)gui.width, (float)gui.height);
    io.DeltaTime = gpu.frameTimer;
    draw();
}

void Photon::draw(){
    prepareFrame();
    if (!prepared) { return; }
    gui.buildCommandBuffers(gpu.vulkanDevice, gpu.renderPass, gpu.frameBuffers, gpu.vulkanSwapchain.drawCmdBuffers, &gpu);
    gpu.submitInfo.commandBufferCount = 1;
    gpu.submitInfo.pCommandBuffers = &gpu.vulkanSwapchain.drawCmdBuffers[gpu.currentBuffer];
    VK_CHECK(vkQueueSubmit(gpu.vulkanDevice.graphicsQueue, 1, &gpu.submitInfo, VK_NULL_HANDLE));
    
    // TODO: To properly display the rendered 3D frame in ImGui without recursion:
    // 1. Render the 3D scene to an off-screen framebuffer (renderedFrame.framebuffer)
    // 2. Display that framebuffer texture in the ImGui window
    // 3. Render ImGui to the swap chain
    // Currently the swap chain image includes ImGui, causing recursion if we copy it.
    
    submitFrame();
}

void Photon::prepareFrame(){
    // Acquire the next image from the swap chain
	VkResult result = gpu.vulkanSwapchain.acquireNextImage(gpu.vulkanDevice.logicalDevice, gpu.semaphores.presentComplete, &gpu.currentBuffer);
	if (result == VK_ERROR_SURFACE_LOST_KHR) {
        logs("[!] Swap chain surface lost; stopping rendering loop");
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
        logs("[!] Swap chain surface lost during present; stopping rendering loop");
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
    VkResult swapchainResult = gpu.vulkanSwapchain.createSwapChain(&gui.width, &gui.height, gui.settings.vsync, gui.settings.fullscreen, gui.settings.transparent, gpu.vulkanDevice.physicalDevice, gpu.vulkanDevice.logicalDevice);
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
    gui.buildCommandBuffers(gpu.vulkanDevice, gpu.renderPass, gpu.frameBuffers, gpu.vulkanSwapchain.drawCmdBuffers, &gpu);

    for (auto& fence : gpu.waitFences) { vkDestroyFence(gpu.vulkanDevice.logicalDevice, fence, nullptr); }
    gpu.createSynchronizationPrimitives(gpu.vulkanDevice.logicalDevice, gpu.vulkanSwapchain.drawCmdBuffers);
    vkDeviceWaitIdle(gpu.vulkanDevice.logicalDevice);
    if ((gui.width > 0.0f) && (gui.height > 0.0f)) { gpu.camera.updateAspectRatio((float)gui.width / (float)gui.height); }
    gui.resized = true;
    prepared = true;
}
