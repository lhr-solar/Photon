/*[Δ] the photon heterogenous compute engine*/
#include <thread>
#include <iostream>
#include <cmath>

#include "photon.hpp"
#include "include.hpp"
#include "../gpu/gpu.hpp"
#include "../gui/gui.hpp"
#include "imgui.h"

#include "osmChunkFormat.hpp"

#include <filesystem>

namespace {
    constexpr double kOSMChunkSizeMeters = 128.0;
    constexpr int kOSMChunkRadius = 2;
    constexpr const char* kOSMChunkDir = "cache/osm_chunks_3857";

    static ChunkId globalChunkFromCamera(const glm::vec3& cameraLocalMeters, const glm::dvec2& originMerc, double chunkSizeMeters) {
        glm::dvec2 globalXZ = originMerc + glm::dvec2(cameraLocalMeters.x, cameraLocalMeters.z);
        const double inv = 1.0 / chunkSizeMeters;
        int32_t cx = static_cast<int32_t>(std::floor(globalXZ.x * inv));
        int32_t cz = static_cast<int32_t>(std::floor(globalXZ.y * inv));
        return ChunkId{cx, cz};
    }
}

Photon::Photon(){ 
    logs("[+] Constructing Photon"); 
    gui.ui.networkINTF = &network;

    chunks.setDiskCacheDir(kOSMChunkDir);
    chunks.setChunkSizeMeters(static_cast<float>(kOSMChunkSizeMeters));
    chunks.setRadiusChunks(kOSMChunkRadius);
    chunks.setRamBudgetBytes(256ull * 1024 * 1024);
    chunks.setGenerateIfMissing(false);
    // Default origin from UI ("street view" default location)
    osmOriginLat = gui.ui.renderSettings.osmLat;
    osmOriginLon = gui.ui.renderSettings.osmLon;
    {
        glm::dvec2 c = OSMLoader::latLonToMercatorMeters(osmOriginLat, osmOriginLon);
        chunks.setWorldOriginMeters(c.x, c.y);
    }

    // Base elevation at origin (used as a simple Y-offset for OSM + ground plane)
    {
        float elev = 0.0f;
        osmLoader.getElevationAt(osmOriginLat, osmOriginLon, elev);
        gpu.osmElevationOffset = elev;
        gui.ui.renderSettings.modelPosition.y = elev;
    }
    chunks.start();

    // Start on-demand chunk fetcher thread
    osmChunkFetchRunning = true;
    osmChunkFetchThread = std::thread([this]() {
        OSMLoader loader;
        while (osmChunkFetchRunning.load(std::memory_order_relaxed)) {
            ChunkId id{};
            {
                std::unique_lock lk(osmChunkFetchMtx);
                osmChunkFetchCv.wait(lk, [&]() {
                    return !osmChunkFetchRunning.load(std::memory_order_relaxed) || !osmChunkFetchQueue.empty();
                });
                if (!osmChunkFetchRunning.load(std::memory_order_relaxed)) {
                    break;
                }
                id = osmChunkFetchQueue.front();
                osmChunkFetchQueue.pop_front();
            }

            const bool ok = loader.fetchChunkToDisk(kOSMChunkDir, osmOriginLat, osmOriginLon, id, kOSMChunkSizeMeters);
            {
                std::scoped_lock lk(osmStatusMtx);
                osmDiskStatus = ok ? loader.getStatus() : ("Chunk fetch failed: " + loader.getStatus());
            }
            {
                std::scoped_lock lk(osmChunkFetchMtx);
                osmChunkFetchInFlight.erase(id);
            }
        }
    });
};
Photon::~Photon(){ 
    logs("[!] Destructuring Photon");
    
    // Signal cancellation to OSM loader
    osmLoader.cancel();

    // Stop on-demand chunk fetcher
    osmChunkFetchRunning = false;
    osmChunkFetchCv.notify_all();
    if (osmChunkFetchThread.joinable()) {
        osmChunkFetchThread.join();
    }

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
        osmLoadInProgress = false; // streaming mode: no full-region background build

        // Reset any previously streamed chunk meshes
        for (auto& model : gpu.osmModels) {
            for (auto& mesh : model.meshes) {
                mesh.vertexBuffer.destroy();
                mesh.indexBuffer.destroy();
            }
        }
        gpu.osmModels.clear();
        osmChunkToModelIndex.clear();
        osmModelIndexToChunkId.clear();
        osmDiskReady = false;


        // Reset origin to requested center ("street view" location)
        osmOriginLat = gui.ui.renderSettings.osmLat;
        osmOriginLon = gui.ui.renderSettings.osmLon;
        {
            glm::dvec2 c = OSMLoader::latLonToMercatorMeters(osmOriginLat, osmOriginLon);
            chunks.clear();
            chunks.setDiskCacheDir(kOSMChunkDir);
            chunks.setGenerateIfMissing(false);
            chunks.setWorldOriginMeters(c.x, c.y);
        }

        // Clear pending fetches
        {
            std::scoped_lock lk(osmChunkFetchMtx);
            osmChunkFetchQueue.clear();
            osmChunkFetchInFlight.clear();
        }

        // Spawn car at origin (local meters) so camera immediately streams nearby chunks
        {
            float elev = 0.0f;
            osmLoader.getElevationAt(osmOriginLat, osmOriginLon, elev);
            gpu.osmElevationOffset = elev;
            gui.ui.renderSettings.modelPosition = glm::vec3(0.0f, elev, 0.0f);
        }
        gui.ui.renderSettings.modelRotation = glm::vec3(0.0f, 0.0f, 0.0f);
	    

        {
            std::scoped_lock lk(osmStatusMtx);
            osmDiskStatus = "OSM streaming enabled; fetching chunks on demand";
        }
	}

    // Update displayed OSM status from disk chunking stage
    {
        std::string statusCopy;
        {
            std::scoped_lock lk(osmStatusMtx);
            statusCopy = osmDiskStatus;
        }
        if (!statusCopy.empty()) {
            gpu.osmStatus = statusCopy;
        }
    }

    // Pull any ready chunk bytes and upload to GPU as OSM models
    {
        auto ready = chunks.drainReadyChunks();
        if (!ready.empty()) {
            for (auto& item : ready) {
                const ChunkId id = item.first;
                const std::vector<uint8_t>& bytes = item.second;
                if (osmChunkToModelIndex.find(id) != osmChunkToModelIndex.end()) {
                    continue;
                }
                if (bytes.size() < sizeof(OSMChunkFileHeader)) {
                    continue;
                }
                const auto* hdr = reinterpret_cast<const OSMChunkFileHeader*>(bytes.data());
                if (!(hdr->magic[0] == 'O' && hdr->magic[1] == 'S' && hdr->magic[2] == 'M' && hdr->magic[3] == 'C')) {
                    continue;
                }
                const size_t vCount = static_cast<size_t>(hdr->vertexCount);
                const size_t iCount = static_cast<size_t>(hdr->indexCount);
                const size_t need = sizeof(OSMChunkFileHeader) + vCount * sizeof(vertex) + iCount * sizeof(uint32_t);
                if (need > bytes.size() || vCount == 0 || iCount == 0) {
                    continue;
                }
                const uint8_t* p = bytes.data() + sizeof(OSMChunkFileHeader);
                const vertex* verts = reinterpret_cast<const vertex*>(p);
                p += vCount * sizeof(vertex);
                const uint32_t* inds = reinterpret_cast<const uint32_t*>(p);

                Model model;
                model.name = "osm_chunk_" + std::to_string(id.x) + "_" + std::to_string(id.z);
                model.vertices.assign(verts, verts + vCount);
                model.indices.assign(inds, inds + iCount);

                Model::Mesh mesh;
                mesh.vertexCount = static_cast<uint32_t>(vCount);
                mesh.indexCount = static_cast<uint32_t>(iCount);

                const VkDeviceSize vbSize = model.vertices.size() * sizeof(vertex);
                const VkDeviceSize ibSize = model.indices.size() * sizeof(uint32_t);
                gpu.vulkanDevice.createBuffer(
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    &mesh.vertexBuffer,
                    vbSize,
                    (void*)model.vertices.data());
                gpu.vulkanDevice.createBuffer(
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    &mesh.indexBuffer,
                    ibSize,
                    (void*)model.indices.data());

                Primitive prim{};
                prim.firstIndex = 0;
                prim.indexCount = static_cast<uint32_t>(model.indices.size());
                prim.materialIndex = 0;
                mesh.primitives.push_back(prim);
                model.meshes.push_back(std::move(mesh));

                gpu.osmModels.push_back(std::move(model));
                osmChunkToModelIndex.emplace(id, gpu.osmModels.size() - 1);
                osmModelIndexToChunkId.push_back(id);
            }
        }
    }
	
    // Update camera to follow car and look at it (street-view-ish defaults)
    {
        const float followDistance = 8.0f; // meters behind
        const float followHeight   = 4.0f;  // approx eye height

        glm::vec3 carPos = gui.ui.renderSettings.modelPosition;
        glm::vec3 rotDeg = gui.ui.renderSettings.modelRotation;
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

        // Stable follow camera: yaw-only around world-up.
        // Treat car forward as +X in world space after yaw.
        glm::mat4 yawM(1.0f);
        yawM = glm::rotate(yawM, glm::radians(rotDeg.y), worldUp);
        glm::vec3 forward = glm::normalize(glm::vec3(yawM * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));

        // Position the camera a fixed distance behind and above the car
        glm::vec3 camPos = carPos - forward * followDistance + worldUp * followHeight;
        gpu.camera.position = camPos;

        // Aim the camera at the car using world-up to avoid roll/flips
        gpu.camera.setViewTarget(carPos, worldUp);
    }

    // Request missing chunks around camera from Overpass (deduped)
    {
        const glm::dvec2 originMerc = OSMLoader::latLonToMercatorMeters(osmOriginLat, osmOriginLon);
        const ChunkId center = globalChunkFromCamera(gpu.camera.position, originMerc, kOSMChunkSizeMeters);

        for (int dz = -kOSMChunkRadius; dz <= kOSMChunkRadius; ++dz) {
            for (int dx = -kOSMChunkRadius; dx <= kOSMChunkRadius; ++dx) {
                ChunkId id{center.x + dx, center.z + dz};
                const std::string path = std::string(kOSMChunkDir) + "/chunk_" + std::to_string(id.x) + "_" + std::to_string(id.z) + ".bin";
                if (std::filesystem::exists(path)) {
                    continue;
                }
                std::scoped_lock lk(osmChunkFetchMtx);
                if (osmChunkFetchInFlight.insert(id).second) {
                    osmChunkFetchQueue.push_back(id);
                    osmChunkFetchCv.notify_one();
                }
            }
        }
    }

    // Chunk streaming around camera
    if (!osmLoadInProgress) {
        chunks.updateFromCamera(gpu.camera.position, gpu.frameCounter);
    }

    // Evict GPU OSM chunk meshes that are no longer desired
    {
        auto removeAt = [&](size_t idx) {
            if (idx >= gpu.osmModels.size()) return;

            // Destroy buffers for the removed model
            for (auto& mesh : gpu.osmModels[idx].meshes) {
                mesh.vertexBuffer.destroy();
                mesh.indexBuffer.destroy();
            }

            const size_t last = gpu.osmModels.size() - 1;
            if (idx != last) {
                std::swap(gpu.osmModels[idx], gpu.osmModels[last]);
                std::swap(osmModelIndexToChunkId[idx], osmModelIndexToChunkId[last]);

                // Fix mapping for the model we swapped into idx
                osmChunkToModelIndex[osmModelIndexToChunkId[idx]] = idx;
            }
            gpu.osmModels.pop_back();
            osmModelIndexToChunkId.pop_back();
        };

        for (auto it = osmChunkToModelIndex.begin(); it != osmChunkToModelIndex.end(); ) {
            const ChunkId id = it->first;
            const size_t idx = it->second;
            if (!chunks.isDesired(id)) {
                removeAt(idx);
                it = osmChunkToModelIndex.erase(it);
                continue;
            }
            ++it;
        }
    }

    if ((gpu.frameCounter % 300) == 0) {
        logs("[Chunk] resident CPU chunks: " << chunks.residentChunkCount());
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
