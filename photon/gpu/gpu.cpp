#ifndef APPLE
#include <SDL3/SDL_oldnames.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <vulkan_core.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#ifdef _WIN32
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <vulkan/vulkan_win32.h>
#endif
#include "ui_frag_spv.hpp"
#include "ui_vert_spv.hpp"
#include "imgui.h"
#include "implot3d.h"

#include "gpu.hpp"

void GPU::init() {
    uint32_t count = 0;
    std::vector<const char *> enabledLayers{};
    std::vector<VkExtensionProperties> extensionProperties{};
    std::vector<VkLayerProperties> layerProperties{};
    std::vector<VkPhysicalDevice> physicalDevices{};
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};
    std::vector<const char*> deviceExtensions{};
    vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    extensionProperties.resize(count);
    vkEnumerateInstanceExtensionProperties(NULL, &count, extensionProperties.data());
    vkEnumerateInstanceLayerProperties(&count, NULL);
    layerProperties.resize(count);
    vkEnumerateInstanceLayerProperties(&count, layerProperties.data());

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Vulkan_LoadLibrary(NULL);
    window = createWindow();
#ifdef _WIN32
    configureTransparentWindow();
#endif
    const char *const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&count);
    std::vector<const char *> enabledExtensions(sdlExtensions, sdlExtensions + count);
    enabledExtensions.push_back( VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef _WIN32
    enabledExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
#endif
    if(validationLayerSupport())enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo applicationInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "Photon",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = NULL,
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    if(validationLayerSupport()) enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
    VkInstanceCreateInfo instanceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = static_cast<uint32_t>(enabledLayers.size()),
        .ppEnabledLayerNames = enabledLayers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size()),
        .ppEnabledExtensionNames = enabledExtensions.data(),
    }; 
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if(validationLayerSupport()){
        populateDebugMessengerCreateInfo(&debugCreateInfo);
        instanceCreateInfo.pNext = (const void*)&debugCreateInfo;
    }
    vkCreateInstance(&instanceCreateInfo, NULL, &instance);
    SDL_Vulkan_CreateSurface(window, instance, NULL, &surface);

    vkEnumeratePhysicalDevices(instance, &count, NULL);
    physicalDevices.resize(count);
    vkEnumeratePhysicalDevices(instance, &count, physicalDevices.data());
    physicalDevice = physicalDevices[0];
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemoryProperties);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, NULL);
    deviceQueueFamilyProperties.resize(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, deviceQueueFamilyProperties.data());
#ifdef _WIN32
    queryPhysicalDeviceId();
#endif
    for(int i = 0; i < deviceQueueFamilyProperties.size(); i++){
        const auto & p = deviceQueueFamilyProperties[i];
        if((p.queueFlags & VK_QUEUE_GRAPHICS_BIT) && (p.queueCount >= 1)){
            queueFamilyIndex = i; queueCount = 1; break;
        }
    }
    VkDeviceQueueCreateInfo graphicsQueueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueFamilyIndex = queueFamilyIndex,
        .queueCount = queueCount,
        .pQueuePriorities = &queuePriority,
    }; queueCreateInfos.push_back(graphicsQueueCreateInfo);
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifdef _WIN32
    deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME);
#endif
    VkDeviceCreateInfo deviceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data(),
        .enabledLayerCount = 0,         //deprecated
        .ppEnabledLayerNames = NULL,    //deprecated
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
        .pEnabledFeatures = NULL,
    }; vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &device);
    vkGetDeviceQueue(device, queueFamilyIndex, queueIndex, &queue);

    createSwapchainResources();
    VkAttachmentDescription attachmentDescription = {
        .flags = 0,
        .format = swapchainFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR 
    }; attachmentDescriptions.push_back(attachmentDescription);
    VkAttachmentReference colorAttachmentReference = {
        .attachment = static_cast<uint32_t>(attachmentDescriptions.size() - 1),
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    VkSubpassDescription subpassDescription = {
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = NULL,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentReference,
        .pResolveAttachments = 0,
        .pDepthStencilAttachment = 0,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = 0,
    }; subpassDescriptions.push_back(subpassDescription);
    VkSubpassDependency subpassDependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = 0,
    }; subpassDependencies.push_back(subpassDependency);
    VkRenderPassCreateInfo renderPassCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size()),
        .pAttachments = attachmentDescriptions.data(),
        .subpassCount = static_cast<uint32_t>(subpassDescriptions.size()),
        .pSubpasses = subpassDescriptions.data(),
        .dependencyCount = static_cast<uint32_t>(subpassDependencies.size()),
        .pDependencies = subpassDependencies.data(),
    }; vkCreateRenderPass(device, &renderPassCreateInfo, NULL, &renderpass);
    destroySwapchainResources();
#ifdef _WIN32
    tryActivateDirectComposition(3);
#else
    createSwapchainResources();
#endif
    createFrameResources();
};

void GPU::createSwapchainResources(){
    uint32_t drawableWidth = 0;
    uint32_t drawableHeight = 0;
    queryWindowPixelSize(drawableWidth, drawableHeight);

    VkSurfaceCapabilitiesKHR surfaceCapabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);

    if (surfaceCapabilities.currentExtent.width != UINT32_MAX) {
        width = surfaceCapabilities.currentExtent.width;
        height = surfaceCapabilities.currentExtent.height;
    } else {
        width = drawableWidth;
        height = drawableHeight;
        width = std::max(surfaceCapabilities.minImageExtent.width, std::min(surfaceCapabilities.maxImageExtent.width, width));
        height = std::max(surfaceCapabilities.minImageExtent.height, std::min(surfaceCapabilities.maxImageExtent.height, height));
    }

    uint32_t count = 0;
    std::vector<VkSurfaceFormatKHR> surfaceFormats{};
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, NULL);
    surfaceFormats.resize(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, surfaceFormats.data());
    swapchainFormat = surfaceFormats[0].format;
    swapchainColorspace = surfaceFormats[0].colorSpace;
    for (const auto& f : surfaceFormats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM) && (f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)) {
            swapchainFormat = f.format;
            swapchainColorspace = f.colorSpace;
            break;
        }
    }

    std::vector<VkPresentModeKHR> presentModes{};
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, NULL);
    presentModes.resize(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, presentModes.data());
    presentationMode = VK_PRESENT_MODE_FIFO_KHR;
    for(const auto& p : presentModes)
        if (p == VK_PRESENT_MODE_MAILBOX_KHR) { presentationMode = p; break; }

    uint32_t imageCount = surfaceCapabilities.minImageCount + 1;
    if((surfaceCapabilities.maxImageCount > 0) && (imageCount > surfaceCapabilities.maxImageCount))
        imageCount = surfaceCapabilities.maxImageCount;

    VkCompositeAlphaFlagBitsKHR compositeAlpha = pickCompositeAlpha(surfaceCapabilities);

    VkSwapchainCreateInfoKHR swapchainCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .surface = surface,
        .minImageCount = imageCount,
        .imageFormat = swapchainFormat,
        .imageColorSpace = swapchainColorspace,
        .imageExtent = {width, height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = compositeAlpha,
        .presentMode = presentationMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE
    };
    vkCreateSwapchainKHR(device, &swapchainCreateInfo, NULL, &swapchain);

    vkGetSwapchainImagesKHR(device, swapchain, &count, NULL);
    swapchainImages.resize(count);
    vkGetSwapchainImagesKHR(device, swapchain, &count, swapchainImages.data());

    swapchainImageViews.resize(swapchainImages.size());
    framebuffer.assign(swapchainImages.size(), VK_NULL_HANDLE);
    for(int i = 0; i < swapchainImages.size(); i++){
        VkImageViewCreateInfo imageViewCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .image = swapchainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchainFormat,
            .components = {},
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        vkCreateImageView(device, &imageViewCreateInfo, NULL, &swapchainImageViews[i]);

        if(renderpass != VK_NULL_HANDLE){
            const VkImageView imageView = swapchainImageViews[i];
            VkFramebufferCreateInfo framebufferCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
                .renderPass = renderpass,
                .attachmentCount = 1,
                .pAttachments = &imageView,
                .width = width,
                .height = height,
                .layers = 1,
            };
            vkCreateFramebuffer(device, &framebufferCreateInfo, NULL, &framebuffer[i]);
        }
    }
}

void GPU::destroySwapchainResources(){
#ifdef _WIN32
    releasePresentationResources();
#endif
    for (auto& fb : framebuffer)
        if (fb != VK_NULL_HANDLE) { vkDestroyFramebuffer(device, fb, NULL); fb = VK_NULL_HANDLE; }
    framebuffer.clear();

    for (auto& view : swapchainImageViews)
        if (view != VK_NULL_HANDLE) { vkDestroyImageView(device, view, NULL); view = VK_NULL_HANDLE; }
    swapchainImageViews.clear();
    swapchainImages.clear();

    if (swapchain != VK_NULL_HANDLE) { vkDestroySwapchainKHR(device, swapchain, NULL); swapchain = VK_NULL_HANDLE; }
}

void GPU::createFrameResources(){
    if (commandPool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo commandPoolCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = queueFamilyIndex,
        };
        vkCreateCommandPool(device, &commandPoolCreateInfo, NULL, &commandPool);
    }

    renderCompleteSemaphores.assign(swapchainImages.size(), VK_NULL_HANDLE);
    imageAvailableSemaphores.assign(swapchainImages.size(), VK_NULL_HANDLE);
    fences.assign(swapchainImages.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo sCI = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (auto& s : renderCompleteSemaphores) vkCreateSemaphore(device, &sCI, NULL, &s);
    for (auto& s : imageAvailableSemaphores) vkCreateSemaphore(device, &sCI, NULL, &s);

    VkFenceCreateInfo fCI = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    for (auto& f : fences) vkCreateFence(device, &fCI, NULL, &f);

    commandBuffers.assign(swapchainImages.size(), VK_NULL_HANDLE);
    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(commandBuffers.size()),
    };
    vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, commandBuffers.data());

    vertexBuffers.assign(swapchainImages.size(), VK_NULL_HANDLE);
    indexBuffers.assign(swapchainImages.size(), VK_NULL_HANDLE);
    vertexCounts.assign(swapchainImages.size(), 0);
    indexCounts.assign(swapchainImages.size(), 0);
    vertexBufferSizes.assign(swapchainImages.size(), 0);
    indexBufferSizes.assign(swapchainImages.size(), 0);
    vertexBufferMapped.assign(swapchainImages.size(), nullptr);
    indexBufferMapped.assign(swapchainImages.size(), nullptr);
    vertexBufferMemories.assign(swapchainImages.size(), VK_NULL_HANDLE);
    indexBufferMemories.assign(swapchainImages.size(), VK_NULL_HANDLE);
    vertexIsMapped.assign(swapchainImages.size(), 0);
    indexIsMapped.assign(swapchainImages.size(), 0);
}

void GPU::destroyFrameResources(){
    for (size_t i = 0; i < vertexBuffers.size(); i++){
        if ((i < vertexIsMapped.size()) && vertexIsMapped[i] && (i < vertexBufferMemories.size()) && (vertexBufferMemories[i] != VK_NULL_HANDLE))
            vkUnmapMemory(device, vertexBufferMemories[i]);
        if ((i < indexIsMapped.size()) && indexIsMapped[i] && (i < indexBufferMemories.size()) && (indexBufferMemories[i] != VK_NULL_HANDLE))
            vkUnmapMemory(device, indexBufferMemories[i]);
        if ((i < vertexBuffers.size()) && (vertexBuffers[i] != VK_NULL_HANDLE)) vkDestroyBuffer(device, vertexBuffers[i], NULL);
        if ((i < indexBuffers.size()) && (indexBuffers[i] != VK_NULL_HANDLE)) vkDestroyBuffer(device, indexBuffers[i], NULL);
        if ((i < vertexBufferMemories.size()) && (vertexBufferMemories[i] != VK_NULL_HANDLE)) vkFreeMemory(device, vertexBufferMemories[i], NULL);
        if ((i < indexBufferMemories.size()) && (indexBufferMemories[i] != VK_NULL_HANDLE)) vkFreeMemory(device, indexBufferMemories[i], NULL);
    }

    if (!commandBuffers.empty())
        vkFreeCommandBuffers(device, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());

    for(auto& s : renderCompleteSemaphores){
        if (s != VK_NULL_HANDLE) vkDestroySemaphore(device, s, NULL);
    }
    for(auto& s : imageAvailableSemaphores){
        if (s != VK_NULL_HANDLE) vkDestroySemaphore(device, s, NULL);
    }
    for(auto& f : fences){
        if (f != VK_NULL_HANDLE) vkDestroyFence(device, f, NULL);
    }

    commandBuffers.clear();
    renderCompleteSemaphores.clear();
    imageAvailableSemaphores.clear();
    fences.clear();
    vertexBuffers.clear();
    indexBuffers.clear();
    vertexCounts.clear();
    indexCounts.clear();
    vertexBufferSizes.clear();
    indexBufferSizes.clear();
    vertexBufferMapped.clear();
    indexBufferMapped.clear();
    vertexBufferMemories.clear();
    indexBufferMemories.clear();
    vertexIsMapped.clear();
    indexIsMapped.clear();
}

void GPU::imguiBackend(){
    ImGui::CreateContext();
    ImPlot3D::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.IniFilename = nullptr;
    io.ConfigFlags  |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags  |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    //io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    unsigned char *fontData = nullptr;
    int texWidth = 0;
    int texHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    VkDeviceSize uploadSize = texWidth * texHeight * 4 * sizeof(char);
    VkImageCreateInfo imageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {(uint32_t)texWidth, (uint32_t)texHeight, 1,},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = 0,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    }; vkCreateImage(device, &imageCreateInfo, NULL, &fontImage);

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, fontImage, &memReqs);
    VkMemoryAllocateInfo memAllocInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = NULL,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    }; vkAllocateMemory(device, &memAllocInfo, NULL, &fontMemory);
    vkBindImageMemory(device, fontImage, fontMemory, 0);

    VkImageViewCreateInfo imageViewCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .image = fontImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .components = {},
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        }
    }; vkCreateImageView(device, &imageViewCreateInfo, nullptr, &fontView);

    VkBuffer stagingBuffer;
    memReqs = {};
    VkMemoryAllocateInfo memAlloc{};
    VkDeviceMemory tempMemory{};
    void* mapped;
    VkBufferCreateInfo bufferCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = uploadSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    }; vkCreateBuffer(device, &bufferCreateInfo, NULL, &stagingBuffer);
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device, &memAlloc, nullptr, &tempMemory);
    vkBindBufferMemory(device, stagingBuffer, tempMemory, 0);
    vkMapMemory(device, tempMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
    memcpy(mapped, fontData, uploadSize);
    vkUnmapMemory(device, tempMemory);

    VkCommandBufferBeginInfo commandBeginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    commandBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffers[0], &commandBeginInfo);
    VkImageSubresourceRange imageSubresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .layerCount = 1,
    };
    setImageLayout(commandBuffers[0], fontImage, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, imageSubresourceRange, VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy bufferCopyRegion = {};
    bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bufferCopyRegion.imageSubresource.layerCount = 1;
    bufferCopyRegion.imageExtent.width = texWidth;
    bufferCopyRegion.imageExtent.height = texHeight;
    bufferCopyRegion.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(commandBuffers[0], stagingBuffer, fontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);
    setImageLayout(commandBuffers[0], fontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, imageSubresourceRange, VK_PIPELINE_STAGE_TRANSFER_BIT, 
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vkEndCommandBuffer(commandBuffers[0]);
    VkSubmitInfo submitInfo {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[0];
    VkFenceCreateInfo fenceCreateInfo {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = 0;
    VkFence fence; vkCreateFence(device, &fenceCreateInfo, nullptr, &fence);
    vkQueueSubmit(queue, 1, &submitInfo, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT32_MAX);
    vkDestroyFence(device, fence, NULL);
    vkDestroyBuffer(device, stagingBuffer, NULL);
    vkFreeMemory(device, tempMemory, NULL);
    VkSamplerCreateInfo samplerCreateInfo {};
    samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCreateInfo.maxAnisotropy = 1.0f;
    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    vkCreateSampler(device, &samplerCreateInfo, nullptr, &fontSampler);
    VkDescriptorImageInfo fontDescriptorImageInfo {};
    fontDescriptorImageInfo.sampler = fontSampler;
    fontDescriptorImageInfo.imageView = fontView;
    fontDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // descriptor s here
    VkDescriptorPoolSize descriptorPoolSize {};
    descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorPoolSize.descriptorCount = 8;
    std::vector<VkDescriptorPoolSize> poolSizes = { descriptorPoolSize };
    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolInfo.pPoolSizes = poolSizes.data();
    descriptorPoolInfo.maxSets = 8;
    vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool);
    VkDescriptorSetLayoutBinding setLayoutBinding0 {};
    setLayoutBinding0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    setLayoutBinding0.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    setLayoutBinding0.binding = 0;
    setLayoutBinding0.descriptorCount = 1;
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {setLayoutBinding0};
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
    descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCreateInfo.pBindings = setLayoutBindings.data();
    descriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
    vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout);
    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo {};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = descriptorPool;
    descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &descriptorSet);

    VkWriteDescriptorSet fontWriteDescriptorSet {};
    fontWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    fontWriteDescriptorSet.dstSet = descriptorSet;
    fontWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    fontWriteDescriptorSet.dstBinding = 0;
    fontWriteDescriptorSet.pImageInfo = &fontDescriptorImageInfo;
    fontWriteDescriptorSet.descriptorCount = 1;

    vkUpdateDescriptorSets(device, 1, &fontWriteDescriptorSet, 0, nullptr);
    io.Fonts->SetTexID(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(descriptorSet)));

    VkPushConstantRange pushConstantRange {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(imguiPushConst);
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo {};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
    vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &imguiPipelineLayout);

    VkPipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo {};
    pipelineInputAssemblyStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    pipelineInputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineInputAssemblyStateCreateInfo.flags = 0;
    pipelineInputAssemblyStateCreateInfo.primitiveRestartEnable = VK_FALSE;

    VkPipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo {};
    pipelineRasterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    pipelineRasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
    pipelineRasterizationStateCreateInfo.cullMode = VK_CULL_MODE_NONE;
    pipelineRasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipelineRasterizationStateCreateInfo.flags = 0;
    pipelineRasterizationStateCreateInfo.depthClampEnable = VK_FALSE;
    pipelineRasterizationStateCreateInfo.lineWidth = 1.0f;

    VkPipelineColorBlendAttachmentState blendAttachmentState{};
    blendAttachmentState.blendEnable = VK_TRUE;
    blendAttachmentState.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachmentState.dstColorBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo {};
    pipelineColorBlendStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    pipelineColorBlendStateCreateInfo.attachmentCount = 1;
    pipelineColorBlendStateCreateInfo.pAttachments = &blendAttachmentState;

    VkPipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo {};
    pipelineDepthStencilStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    pipelineDepthStencilStateCreateInfo.depthTestEnable = VK_FALSE;
    pipelineDepthStencilStateCreateInfo.depthWriteEnable = VK_FALSE;
    pipelineDepthStencilStateCreateInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    pipelineDepthStencilStateCreateInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo {};
    pipelineMultisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    pipelineMultisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    pipelineMultisampleStateCreateInfo.flags = 0;

    VkViewport viewport;
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor;
    scissor.offset = {};
    scissor.extent.width = width;
    scissor.extent.height = height;

    VkPipelineViewportStateCreateInfo pipelineViewportStateCreateInfo;
    pipelineViewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    pipelineViewportStateCreateInfo.pNext = NULL;
    pipelineViewportStateCreateInfo.flags = 0;
    pipelineViewportStateCreateInfo.viewportCount = 1;
    pipelineViewportStateCreateInfo.pViewports = &viewport;
    pipelineViewportStateCreateInfo.scissorCount = 1;
    pipelineViewportStateCreateInfo.pScissors = &scissor;

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo{};
    pipelineDynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    pipelineDynamicStateCreateInfo.pDynamicStates = dynamicStates;
    pipelineDynamicStateCreateInfo.dynamicStateCount = 2;
    pipelineDynamicStateCreateInfo.flags = 0;

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

    VkGraphicsPipelineCreateInfo pipelineCreateInfo {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.layout = imguiPipelineLayout;
    pipelineCreateInfo.renderPass = renderpass;
    pipelineCreateInfo.flags = 0;
    pipelineCreateInfo.basePipelineIndex = -1;
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE; 

    pipelineCreateInfo.pInputAssemblyState = &pipelineInputAssemblyStateCreateInfo;
    pipelineCreateInfo.pRasterizationState = &pipelineRasterizationStateCreateInfo;
    pipelineCreateInfo.pColorBlendState = &pipelineColorBlendStateCreateInfo;
    pipelineCreateInfo.pMultisampleState = &pipelineMultisampleStateCreateInfo;
    pipelineCreateInfo.pViewportState = &pipelineViewportStateCreateInfo;
    pipelineCreateInfo.pDepthStencilState = &pipelineDepthStencilStateCreateInfo;
    pipelineCreateInfo.pDynamicState = &pipelineDynamicStateCreateInfo;
    pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCreateInfo.pStages = shaderStages.data();

    VkVertexInputBindingDescription vInputBindDescription {};
    vInputBindDescription.binding = 0;
    vInputBindDescription.stride = sizeof(ImDrawVert);
    vInputBindDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::vector<VkVertexInputBindingDescription> vertexInputBindings = {vInputBindDescription};

    // binding location format offset
    VkVertexInputAttributeDescription vPositionInputAttribDescription {};
    vPositionInputAttribDescription.location = 0;
    vPositionInputAttribDescription.binding = 0;
    vPositionInputAttribDescription.format = VK_FORMAT_R32G32_SFLOAT;
    vPositionInputAttribDescription.offset = offsetof(ImDrawVert, pos);

    VkVertexInputAttributeDescription vUVInputAttribDescription {};
    vUVInputAttribDescription.location = 1;
    vUVInputAttribDescription.binding = 0;
    vUVInputAttribDescription.format = VK_FORMAT_R32G32_SFLOAT;
    vUVInputAttribDescription.offset = offsetof(ImDrawVert, uv);

    VkVertexInputAttributeDescription vColorInputAttribDescription {};
    vColorInputAttribDescription.location = 2;
    vColorInputAttribDescription.binding = 0;
    vColorInputAttribDescription.format = VK_FORMAT_R8G8B8A8_UNORM;
    vColorInputAttribDescription.offset = offsetof(ImDrawVert, col);

    std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = { 
    vPositionInputAttribDescription, vUVInputAttribDescription, vColorInputAttribDescription };

    VkPipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo {};
    pipelineVertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    pipelineVertexInputStateCreateInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
    pipelineVertexInputStateCreateInfo.pVertexBindingDescriptions = vertexInputBindings.data();
    pipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
    pipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions = vertexInputAttributes.data();

    pipelineCreateInfo.pVertexInputState = &pipelineVertexInputStateCreateInfo;

    shaderStages[0] = loadShader(ui_vert_spv, ui_vert_spv_size, uiShaderVert, VK_SHADER_STAGE_VERTEX_BIT, device);
    shaderStages[1] = loadShader(ui_frag_spv, ui_frag_spv_size, uiShaderIndex, VK_SHADER_STAGE_FRAGMENT_BIT, device);

    vkCreateGraphicsPipelines(device, NULL, 1, &pipelineCreateInfo, nullptr, &imguiPipeline);
    vertexBuffers.resize(swapchainImages.size());
    indexBuffers.resize(swapchainImages.size());
    vertexCounts.resize(swapchainImages.size());
    indexCounts.resize(swapchainImages.size());
    vertexBufferSizes.resize(swapchainImages.size());
    indexBufferSizes.resize(swapchainImages.size());
    vertexBufferMapped.resize(swapchainImages.size());
    indexBufferMapped.resize(swapchainImages.size());
    vertexBufferMemories.resize(swapchainImages.size());
    indexBufferMemories.resize(swapchainImages.size());
    vertexIsMapped.resize(swapchainImages.size());
    indexIsMapped.resize(swapchainImages.size());
}

void GPU::imguiPresentation(uint32_t imgIdx){
    VkClearValue clearValues[1];
    clearValues[0].color = {{0.0f, 0.00f, 0.00f, 0.0f}};
    VkRenderPassBeginInfo renderPassBeginInfo {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderpass;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width = width;
    renderPassBeginInfo.renderArea.extent.height = height;
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = clearValues;

    static VkDeviceSize dedicatedVertexSize = 8 * 1024 * 1024;
    static VkDeviceSize dedicatedIndexSize  = 8 * 1024 * 1024;
    ImDrawData *imDrawData = ImGui::GetDrawData();
    VkDeviceSize newVertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
    VkDeviceSize newIndexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);
    VkDeviceSize& vertexBufferSize = vertexBufferSizes[frameIndex];
    VkDeviceSize& indexBufferSize = indexBufferSizes[frameIndex];

    while(newVertexBufferSize > dedicatedVertexSize) dedicatedVertexSize = dedicatedVertexSize * 2;
    while(newIndexBufferSize > dedicatedIndexSize) dedicatedIndexSize = dedicatedIndexSize * 2;
    newVertexBufferSize = dedicatedVertexSize;
    newIndexBufferSize = dedicatedIndexSize;

    VkBuffer& vertexBuffer = vertexBuffers[frameIndex];
    VkBuffer& indexBuffer = indexBuffers[frameIndex];
    int32_t& vertexCount = vertexCounts[frameIndex];
    int32_t& indexCount = indexCounts[frameIndex];

    // Vertex Buffer
    if ((vertexBuffer == VK_NULL_HANDLE) || (vertexBufferSize < newVertexBufferSize)) {
        vertexBufferSize = newVertexBufferSize;
        uint32_t& isMapped = vertexIsMapped[frameIndex];
        VkDeviceMemory& memory = vertexBufferMemories[frameIndex];
        void*& mapped = vertexBufferMapped[frameIndex];
        if(isMapped){ vkUnmapMemory(device, memory); isMapped = false; }
        if(vertexBuffer) vkDestroyBuffer(device, vertexBuffer, nullptr);
        if(memory) vkFreeMemory(device, memory, nullptr);

        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferCreateInfo.size = vertexBufferSize;
        vkCreateBuffer(device, &bufferCreateInfo, nullptr, &vertexBuffer);
        VkMemoryRequirements memReqs;
        VkMemoryAllocateInfo memAlloc{};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        vkGetBufferMemoryRequirements(device, vertexBuffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 
                                                                       | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        VkMemoryAllocateFlagsInfoKHR allocFlagsInfo{};
        vkAllocateMemory(device, &memAlloc, nullptr, &memory);
        vkBindBufferMemory(device, vertexBuffer, memory, 0);
        vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &vertexBufferMapped[frameIndex]);
        isMapped = true;
    } vertexCount = imDrawData->TotalVtxCount;

    // Index buffer
    if ((indexBuffer == VK_NULL_HANDLE) || (indexBufferSize < newIndexBufferSize)) {
        indexBufferSize = newIndexBufferSize;
        uint32_t& isMapped = indexIsMapped[frameIndex];
        VkDeviceMemory& memory = indexBufferMemories[frameIndex];
        void*& mapped = indexBufferMapped[frameIndex];
        if(isMapped){ vkUnmapMemory(device, memory); isMapped = false; }
        if(indexBuffer) vkDestroyBuffer(device, indexBuffer, nullptr);
        if(memory) vkFreeMemory(device, memory, nullptr);

        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferCreateInfo.size = indexBufferSize;
        vkCreateBuffer(device, &bufferCreateInfo, nullptr, &indexBuffer);
        VkMemoryRequirements memReqs;
        VkMemoryAllocateInfo memAlloc{};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        vkGetBufferMemoryRequirements(device, indexBuffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 
                                                                       | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        VkMemoryAllocateFlagsInfoKHR allocFlagsInfo{};
        vkAllocateMemory(device, &memAlloc, nullptr, &memory);
        vkBindBufferMemory(device, indexBuffer, memory, 0);
        vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &indexBufferMapped[frameIndex]);
        isMapped = true;
    } indexCount = imDrawData->TotalIdxCount;

    ImDrawVert *vtxDst = (ImDrawVert *)vertexBufferMapped[frameIndex];
    ImDrawIdx *idxDst = (ImDrawIdx *)indexBufferMapped[frameIndex];

    for (int n = 0; n < imDrawData->CmdListsCount; n++) {
      const ImDrawList *cmd_list = imDrawData->CmdLists[n];
      memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
      memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
      vtxDst += cmd_list->VtxBuffer.Size;
      idxDst += cmd_list->IdxBuffer.Size;
    }
    // end of updateBuffers

    VkCommandBuffer& commandBuffer = commandBuffers[frameIndex];
    renderPassBeginInfo.framebuffer = framebuffer[imgIdx];
    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, imguiPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, imguiPipeline);
    VkViewport viewport {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = 0.0;
    viewport.maxDepth = 1.0;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    const ImVec2 displayPos = imDrawData->DisplayPos;
    const ImVec2 displaySize = imDrawData->DisplaySize;
    imguiPushConst.scale = glm::vec2(2.0f / displaySize.x, 2.0f / displaySize.y);
    imguiPushConst.translate = glm::vec2(
        -1.0f - displayPos.x * imguiPushConst.scale.x,
        -1.0f - displayPos.y * imguiPushConst.scale.y
    );
    vkCmdPushConstants(commandBuffer, imguiPipelineLayout, 
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock), &imguiPushConst);

    int32_t globalVertexOffset = 0;
    uint32_t globalIndexOffset = 0;

    if (imDrawData->CmdListsCount > 0 && frameIndex < vertexBuffers.size() && frameIndex < indexBuffers.size()) {
        VkBuffer& vertexBuffer = vertexBuffers[frameIndex];
        VkBuffer& indexBuffer = indexBuffers[frameIndex];
        VkDeviceSize offsets[1] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);
        const VkIndexType indexType = (sizeof(ImDrawIdx) == sizeof(uint16_t)) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, indexType);
        VkDescriptorSet boundSet = VK_NULL_HANDLE;
        const ImVec2 clipOff = imDrawData->DisplayPos;
        const ImVec2 clipScale = imDrawData->FramebufferScale;
        for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
            const ImDrawList *cmd_list = imDrawData->CmdLists[i];
            for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {
                const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[j];
                if (pcmd->UserCallback != nullptr) {
                    pcmd->UserCallback(cmd_list, pcmd);
                    continue;
                }
                VkDescriptorSet textureSet = descriptorSet;
                if (pcmd->GetTexID() != 0) textureSet = reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(pcmd->GetTexID()));
                if (textureSet == VK_NULL_HANDLE) textureSet = descriptorSet;
                if (textureSet != boundSet) {
                    vkCmdBindDescriptorSets(commandBuffer, 
                            VK_PIPELINE_BIND_POINT_GRAPHICS, imguiPipelineLayout, 0, 1, &textureSet, 0, nullptr);
                    boundSet = textureSet;
                }
                ImVec4 clipRect;
                clipRect.x = (pcmd->ClipRect.x - clipOff.x) * clipScale.x;
                clipRect.y = (pcmd->ClipRect.y - clipOff.y) * clipScale.y;
                clipRect.z = (pcmd->ClipRect.z - clipOff.x) * clipScale.x;
                clipRect.w = (pcmd->ClipRect.w - clipOff.y) * clipScale.y;
                if (clipRect.x >= width || clipRect.y >= height || clipRect.z <= 0.0f || clipRect.w <= 0.0f) {
                    continue;
                }
                if (clipRect.x < 0.0f) clipRect.x = 0.0f;
                if (clipRect.y < 0.0f) clipRect.y = 0.0f;
                if (clipRect.z > width) clipRect.z = static_cast<float>(width);
                if (clipRect.w > height) clipRect.w = static_cast<float>(height);

                VkRect2D scissorRect;
                scissorRect.offset.x = static_cast<int32_t>(clipRect.x);
                scissorRect.offset.y = static_cast<int32_t>(clipRect.y);
                scissorRect.extent.width = static_cast<uint32_t>(clipRect.z - clipRect.x);
                scissorRect.extent.height = static_cast<uint32_t>(clipRect.w - clipRect.y);
                if (scissorRect.extent.width == 0 || scissorRect.extent.height == 0) continue;
                vkCmdSetScissor(commandBuffer, 0, 1, &scissorRect);
                vkCmdDrawIndexed(commandBuffer, pcmd->ElemCount, 1,
                        globalIndexOffset + static_cast<uint32_t>(pcmd->IdxOffset),
                        globalVertexOffset + static_cast<int32_t>(pcmd->VtxOffset),
                        0);
            }
            globalIndexOffset += static_cast<uint32_t>(cmd_list->IdxBuffer.Size);
            globalVertexOffset += cmd_list->VtxBuffer.Size;
        }
    }
    vkCmdEndRenderPass(commandBuffer);
#ifdef _WIN32
    prepareImageForPresentation(imgIdx);
#endif
//    vkEndCommandBuffer(commandBuffer);
};

void GPU::startFrame(uint32_t& imgIdx){
#ifdef _WIN32
    if(startFramePlatform(imgIdx)) return;
#endif
    vkWaitForFences(device, 1, &fences[frameIndex], VK_TRUE, UINT64_MAX);
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
        imageAvailableSemaphores[frameIndex], VK_NULL_HANDLE, &imgIdx);
    if((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR))
        resizeWindow();
    vkResetFences(device, 1, &fences[frameIndex]);
};

void GPU::resizeWindow(){
    if((window == NULL) || (device == VK_NULL_HANDLE)) return;
    uint32_t newWidth = 0;
    uint32_t newHeight = 0;
    queryWindowPixelSize(newWidth, newHeight);
    while((newWidth == 0) || (newHeight == 0)){
        SDL_Delay(16);
        queryWindowPixelSize(newWidth, newHeight);
    }

#ifdef _WIN32
    if (directCompositionActive){
        const uint32_t desiredImageCount = swapchainImages.empty()
                ? 2u
                : static_cast<uint32_t>(swapchainImages.size());
        if (recreateDirectCompositionTargets(newWidth, newHeight, desiredImageCount)) return;
    }
#endif

    const uint32_t previousImageCount = static_cast<uint32_t>(swapchainImages.size());
    vkDeviceWaitIdle(device);
    destroyFrameResources();
    destroySwapchainResources();
    width = newWidth;
    height = newHeight;
    const uint32_t desiredImageCount = previousImageCount ? previousImageCount : 2;
#ifdef _WIN32
    tryActivateDirectComposition(desiredImageCount);
#else
    createSwapchainResources();
#endif
    createFrameResources();
    frameIndex = 0;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
};

void GPU::submitFrame(const uint32_t imgIdx){
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
#ifdef _WIN32
    adjustSubmitSyncObjects(waitSemaphore, signalSemaphore);
#endif
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphores[frameIndex];
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[frameIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderCompleteSemaphores[imgIdx];
#ifdef _WIN32
    VkWin32KeyedMutexAcquireReleaseInfoKHR keyedInfo{};
    VkDeviceMemory acquireMemory = VK_NULL_HANDLE;
    VkDeviceMemory releaseMemory = VK_NULL_HANDLE;
    uint64_t acquireKey = 0;
    uint64_t releaseKey = 1;
    uint32_t acquireTimeout = INFINITE;
    if(directCompositionActive && imgIdx < directImages.size()){
        acquireMemory = directImages[imgIdx].memory;
        releaseMemory = directImages[imgIdx].memory;
    }
    if(acquireMemory != VK_NULL_HANDLE){
        keyedInfo.sType = VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_KHR;
        keyedInfo.acquireCount = 1;
        keyedInfo.pAcquireSyncs = &acquireMemory;
        keyedInfo.pAcquireKeys = &acquireKey;
        keyedInfo.pAcquireTimeouts = &acquireTimeout;
        keyedInfo.releaseCount = 1;
        keyedInfo.pReleaseSyncs = &releaseMemory;
        keyedInfo.pReleaseKeys = &releaseKey;
        keyedInfo.pNext = submitInfo.pNext;
        submitInfo.pNext = &keyedInfo;
    }
#endif
    vkQueueSubmit(queue, 1, &submitInfo, fences[frameIndex]);
#ifdef _WIN32
    if(presentFramePlatform(imgIdx, frameIndex)) return;
#endif
    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = NULL,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderCompleteSemaphores[imgIdx],
        .swapchainCount = 1,
        .pSwapchains = &swapchain, 
        .pImageIndices = &imgIdx,
        .pResults = NULL
    };
    const VkResult result = vkQueuePresentKHR(queue, &presentInfo);
    if((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) resizeWindow();
}

void GPU::queryWindowPixelSize(uint32_t& outWidth, uint32_t& outHeight) const{
    outWidth = 0;
    outHeight = 0;
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(win32WindowHandle);
    if((hwnd == nullptr) && (window != NULL)){
        SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        if(properties != 0){
            void* hwndProperty = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
            if(hwndProperty != NULL){
                hwnd = static_cast<HWND>(hwndProperty);
            }
        }
    }
    if(hwnd != nullptr){
        RECT rect{};
        if(GetClientRect(hwnd, &rect)){
            const int w = rect.right - rect.left;
            const int h = rect.bottom - rect.top;
            if((w > 0) && (h > 0)){
                outWidth = static_cast<uint32_t>(w);
                outHeight = static_cast<uint32_t>(h);
                return;
            }
        }
    }
#endif
    if(window != NULL){
        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_GetWindowSizeInPixels(window, &drawableWidth, &drawableHeight);
        if((drawableWidth > 0) && (drawableHeight > 0)){
            outWidth = static_cast<uint32_t>(drawableWidth);
            outHeight = static_cast<uint32_t>(drawableHeight);
        }
    }
}

void GPU::destroy() {
    vkDeviceWaitIdle(device);
#ifdef _WIN32
    destroyDirectCompositionPresenter();
#endif
    ImGui::DestroyContext();
    ImPlot3D::DestroyContext();
    destroyFrameResources();
    destroySwapchainResources();
    vkDestroyShaderModule(device, uiShaderVert, NULL);
    vkDestroyShaderModule(device, uiShaderIndex, NULL);
    vkDestroyImage(device, fontImage, NULL);
    vkDestroyImageView(device, fontView, NULL);
    vkDestroySampler(device, fontSampler, NULL);
    vkFreeMemory(device, fontMemory, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyRenderPass(device, renderpass, NULL);
    vkDestroyCommandPool(device, commandPool, NULL);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, NULL);
    vkFreeDescriptorSets(device, descriptorPool, 1, &descriptorSet);
    vkDestroyDescriptorPool(device, descriptorPool, NULL);
    vkDestroyPipeline(device, imguiPipeline, NULL);
    vkDestroyPipelineLayout(device, imguiPipelineLayout, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    SDL_DestroyWindow(window);
    SDL_Quit();
};

uint32_t GPU::getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags propertyFlags){
    for (uint32_t i = 0; i < deviceMemoryProperties.memoryTypeCount; i++){
        if ((typeBits & 1) == 1)
            if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & propertyFlags) == propertyFlags)
                return i;
        typeBits >>= 1;
    }
    return -1;
};

void GPU::setImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
            VkImageSubresourceRange subresourceRange, VkPipelineStageFlags sourceStageMask, VkPipelineStageFlags destinationStageMask){
    VkImageMemoryBarrier imageMemoryBarrier {};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.oldLayout = oldImageLayout;
    imageMemoryBarrier.newLayout = newImageLayout;
    imageMemoryBarrier.image = image;
    imageMemoryBarrier.subresourceRange = subresourceRange;

    switch (oldImageLayout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
	    imageMemoryBarrier.srcAccessMask = 0;
	    break;

	    case VK_IMAGE_LAYOUT_PREINITIALIZED:
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;

        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
        default:
        break;
    }

    switch (newImageLayout) {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;

        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        if (imageMemoryBarrier.srcAccessMask == 0)
            imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
        default:
        break;
    }
    vkCmdPipelineBarrier(commandBuffer, sourceStageMask, destinationStageMask, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
};

VkPipelineShaderStageCreateInfo GPU::loadShader(const uint32_t* code, size_t size, 
        VkShaderModule& module, VkShaderStageFlagBits flagBits, VkDevice device){
    VkShaderModuleCreateInfo moduleCreateInfo{};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = nullptr;
    moduleCreateInfo.flags = 0;
    moduleCreateInfo.codeSize = size;
    moduleCreateInfo.pCode = code;
    vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &module);
    VkPipelineShaderStageCreateInfo shaderStage = {};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = flagBits;
    shaderStage.module = module;
    shaderStage.pName = "main";
    return shaderStage;
};

bool GPU::validationLayerSupport(){
#ifdef NDEBUG
    return false;
#endif 
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    uint32_t layerCount{};
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);
    VkLayerProperties availableLayers[layerCount];
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);
    for(uint32_t i = 0; i < layerCount; i++)
        if(strcmp(availableLayers[i].layerName, validationLayer) == 0)
            return true;
    return false;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, 
        VkDebugUtilsMessageTypeFlagsEXT messageType, 
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, 
        void* pUserData){
    logs("validation layer: " << pCallbackData->pMessage);
    return VK_FALSE;
}

void GPU::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo){
    createInfo->sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo->messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo->messageType =  VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | 
                               VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | 
                               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo->pfnUserCallback = debugCallback;
    createInfo->pUserData = NULL;
}

bool GPU::wantsTransparentSwapchain() const {
    if (window == NULL) return false;
    const Uint64 flags = SDL_GetWindowFlags(window);
    return (flags & SDL_WINDOW_TRANSPARENT) != 0;
}

#ifdef _WIN32
void GPU::prepareImageForPresentation(uint32_t imgIdx){
    if (!directCompositionActive) return;
    if (imgIdx >= swapchainImages.size()) return;
    VkImage image = swapchainImages[imgIdx];
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    VkCommandBuffer commandBuffer = commandBuffers[frameIndex];
    vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void GPU::adjustSubmitSyncObjects(VkSemaphore& waitSemaphore, VkSemaphore& signalSemaphore) const{
    if (!directCompositionActive) return;
    waitSemaphore = VK_NULL_HANDLE;
    signalSemaphore = VK_NULL_HANDLE;
}

bool GPU::presentFramePlatform(uint32_t imgIdx, uint32_t frameSlot){
    if (!directCompositionActive) return false;
    if (frameSlot >= fences.size()) return false;
    if (imgIdx >= directImages.size()) return false;
    vkWaitForFences(device, 1, &fences[frameSlot], VK_TRUE, UINT64_MAX);
    presentWithDirectComposition(imgIdx);
    return true;
}

bool GPU::startFramePlatform(uint32_t& imgIdx){
    if (!directCompositionActive) return false;
    if (frameIndex >= fences.size()) return false;
    vkWaitForFences(device, 1, &fences[frameIndex], VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fences[frameIndex]);
    imgIdx = frameIndex;
    return true;
}

void GPU::forceInitialTransparentResize(){
    if (transparentResizeHackApplied) return;
    if (!wantsTransparentSwapchain()) return;
    if (window == NULL) return;
    int baseWidth = 0;
    int baseHeight = 0;
    SDL_GetWindowSizeInPixels(window, &baseWidth, &baseHeight);
    if ((baseWidth <= 0) || (baseHeight <= 0)) return;
    transparentResizeHackApplied = true;
    const int shrinkWidth = std::max(1, baseWidth - static_cast<int>(baseWidth * 0.5f));
    const int shrinkHeight = std::max(1, baseHeight - static_cast<int>(baseHeight * 0.5f));
    SDL_SetWindowSize(window, shrinkWidth, shrinkHeight);
    SDL_SetWindowSize(window, baseWidth, baseHeight);
}

void GPU::releasePresentationResources(){
    if (directCompositionActive || !directImages.empty()){
        destroyDirectCompositionPresenter();
    }
}

bool GPU::tryActivateDirectComposition(uint32_t imageCount){
    destroyDirectCompositionPresenter();
    if (!wantsTransparentSwapchain()) return false;
    if (!initDirectCompositionPresenter()) return false;
    const uint32_t desiredCount = (imageCount < 2) ? 2u : imageCount;
    if (!createSharedRenderTargets(desiredCount)) {
        destroyDirectCompositionPresenter();
        return false;
    }
    forceInitialTransparentResize();
    return true;
}

SDL_Window* GPU::createWindow(){
    SDL_PropertiesID properties = SDL_CreateProperties();
    if (properties != 0){
        SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Photon");
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
        SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
        SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_TRANSPARENT_BOOLEAN, true);
        SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
        SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
        SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_FOCUSABLE_BOOLEAN, true);
        SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_MOUSE_GRABBED_BOOLEAN, false);
        SDL_Window* created = SDL_CreateWindowWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (created != NULL){
            return created;
        }
    }
    return SDL_CreateWindow("Photon", width, height, 
        SDL_WINDOW_VULKAN | SDL_WINDOW_TRANSPARENT | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
}

VkCompositeAlphaFlagBitsKHR GPU::pickCompositeAlpha(const VkSurfaceCapabilitiesKHR& surfaceCapabilities){
    const bool transparent = wantsTransparentSwapchain();
    VkSurfaceCapabilitiesKHR refreshed = surfaceCapabilities;
    if (transparent &&
        ((surfaceCapabilities.supportedCompositeAlpha & (VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR |
                                                        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR |
                                                        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)) == 0)){
        if ((physicalDevice != VK_NULL_HANDLE) && (surface != VK_NULL_HANDLE)){
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &refreshed);
        }
    }
    const VkCompositeAlphaFlagBitsKHR transparentOrder[] = {
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    const VkCompositeAlphaFlagBitsKHR opaqueOrder[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    };
    if (transparent){
        for (const auto mode : transparentOrder){
            if ((refreshed.supportedCompositeAlpha & mode) != 0){
                return mode;
            }
        }
    }
    for (const auto mode : opaqueOrder){
        if ((refreshed.supportedCompositeAlpha & mode) != 0){
            return mode;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

bool GPU::ensureExternalImageSupport(VkExternalMemoryHandleTypeFlagBits handleType, bool& requiresDedicated){
    requiresDedicated = false;
    if (physicalDevice == VK_NULL_HANDLE) return false;
    VkExternalImageFormatProperties externalProps{};
    externalProps.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;

    VkPhysicalDeviceExternalImageFormatInfo externalInfo{};
    externalInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    externalInfo.handleType = handleType;

    VkPhysicalDeviceImageFormatInfo2 formatInfo{};
    formatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    formatInfo.pNext = &externalInfo;
    formatInfo.format = swapchainFormat;
    formatInfo.type = VK_IMAGE_TYPE_2D;
    formatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    formatInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    formatInfo.flags = 0;

    VkImageFormatProperties2 imageProps{};
    imageProps.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    imageProps.pNext = &externalProps;

    if (vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, &formatInfo, &imageProps) != VK_SUCCESS){
        return false;
    }

    const auto features = externalProps.externalMemoryProperties.externalMemoryFeatures;
    char buffer[128];
    snprintf(buffer, sizeof(buffer),
        "handle=0x%x features=0x%x dedicatedOnly=%s importable=%s exportable=%s",
        handleType,
        features,
        ((features & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) != 0) ? "yes" : "no",
        ((features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0) ? "yes" : "no",
        ((features & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) != 0) ? "yes" : "no");
    if ((features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) == 0){
        return false;
    }
    requiresDedicated = (features & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) != 0;
    return true;
}

bool GPU::selectDirectCompositionHandleType(VkExternalMemoryHandleTypeFlagBits& handleType, bool& requiresDedicated){
    const VkExternalMemoryHandleTypeFlagBits candidates[] = {
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT,
    };
    for (const auto candidate : candidates){
        bool dedicated = false;
        if (ensureExternalImageSupport(candidate, dedicated)){
            handleType = candidate;
            requiresDedicated = dedicated;
            const char* label = (candidate == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT)
                    ? "D3D11_TEXTURE"
                    : "D3D11_TEXTURE_KMT";
            char buffer[96];
            snprintf(buffer, sizeof(buffer), "SelectHandleType: %s dedicatedOnly=%s",
                    label, dedicated ? "yes" : "no");
            return true;
        }
    }
    return false;
}

bool GPU::createSharedHandleForTexture(ID3D11Texture2D* texture,
        VkExternalMemoryHandleTypeFlagBits handleType, HANDLE& sharedHandle){
    sharedHandle = NULL;
    if (texture == nullptr) return false;
    if (handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT){
        IDXGIResource1* resource = nullptr;
        if (FAILED(texture->QueryInterface(IID_PPV_ARGS(&resource)))){
            return false;
        }
        HRESULT hr = resource->CreateSharedHandle(nullptr,
                DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                nullptr, &sharedHandle);
        resource->Release();
        return SUCCEEDED(hr);
    }
    if (handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT){
        IDXGIResource* resource = nullptr;
        if (FAILED(texture->QueryInterface(IID_PPV_ARGS(&resource)))){
            return false;
        }
        HRESULT hr = resource->GetSharedHandle(&sharedHandle);
        resource->Release();
        return SUCCEEDED(hr);
    }
    return false;
}

bool GPU::recreateDirectCompositionTargets(uint32_t pixelWidth, uint32_t pixelHeight, uint32_t imageCount){
    if (!directCompositionActive) return false;
    if (pixelWidth == 0 || pixelHeight == 0) return false;
    const uint32_t desiredCount = (imageCount < 2u) ? 2u : imageCount;
    vkDeviceWaitIdle(device);
    destroyFrameResources();
    destroyDirectCompositionPresenter();
    width = pixelWidth;
    height = pixelHeight;
    if (!initDirectCompositionPresenter()){
        return false;
    }
    if (!createSharedRenderTargets(desiredCount)){
        destroyDirectCompositionPresenter();
        return false;
    }
    createFrameResources();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    frameIndex = 0;
    return true;
}

bool GPU::queryPhysicalDeviceId(){
    physicalDeviceLuidValid = false;
    if (physicalDevice == VK_NULL_HANDLE) return false;
    VkPhysicalDeviceIDProperties idProps{};
    idProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &idProps;
    vkGetPhysicalDeviceProperties2(physicalDevice, &props2);
    if(idProps.deviceLUIDValid == VK_FALSE) return false;
    std::memcpy(physicalDeviceLuid.data(), idProps.deviceLUID, VK_LUID_SIZE);
    physicalDeviceLuidValid = true;
    return true;
}

IDXGIAdapter1* GPU::pickDxgiAdapter(IDXGIFactory2* factory){
    if (factory == nullptr) return nullptr;
    IDXGIAdapter1* adapter = nullptr;
    UINT index = 0;
    while (factory->EnumAdapters1(index++, &adapter) != DXGI_ERROR_NOT_FOUND){
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))){
            if (!physicalDeviceLuidValid ||
                (std::memcmp(physicalDeviceLuid.data(), &desc.AdapterLuid, VK_LUID_SIZE) == 0)){
                return adapter;
            }
        }
        if (adapter){
            adapter->Release();
            adapter = nullptr;
        }
    }
    return nullptr;
}

namespace {
enum class PhotonAccentState : DWORD {
    Disabled = 0,
    Gradient = 1,
    TransparentGradient = 2,
    BlurBehind = 3,
    AcrylicBlurBehind = 4,
};

enum class PhotonWindowCompositionAttrib : DWORD {
    AccentPolicy = 19,
};

struct PhotonAccentPolicy {
    DWORD accentState;
    DWORD accentFlags;
    DWORD gradientColor;
    DWORD animationId;
};

struct PhotonWindowCompositionAttribData {
    PhotonWindowCompositionAttrib attribute;
    PVOID data;
    SIZE_T dataSize;
};

using SetWindowCompositionAttributeFn = BOOL (WINAPI *)(HWND, PhotonWindowCompositionAttribData*);
using DwmExtendFrameIntoClientAreaFn = HRESULT (WINAPI *)(HWND, const MARGINS*);
using DwmEnableBlurBehindWindowFn = HRESULT (WINAPI *)(HWND, const DWM_BLURBEHIND*);

template<typename T>
void SafeRelease(T*& ptr){
    if (ptr){
        ptr->Release();
        ptr = nullptr;
    }
}

using PFN_D3D11CreateDevice = HRESULT (WINAPI *)(
        IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL*, UINT, UINT,
        ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using PFN_CreateDXGIFactory1 = HRESULT (WINAPI *)(REFIID, void**);
using PFN_DCompositionCreateDevice = HRESULT (WINAPI *)(IUnknown*, REFIID, void**);

PFN_D3D11CreateDevice LoadD3D11CreateDevice(){
    static PFN_D3D11CreateDevice fn = [](){
        HMODULE mod = LoadLibraryW(L"d3d11.dll");
        if (!mod) return (PFN_D3D11CreateDevice)nullptr;
        return reinterpret_cast<PFN_D3D11CreateDevice>(GetProcAddress(mod, "D3D11CreateDevice"));
    }();
    return fn;
}

PFN_CreateDXGIFactory1 LoadCreateDXGIFactory1(){
    static PFN_CreateDXGIFactory1 fn = [](){
        HMODULE mod = LoadLibraryW(L"dxgi.dll");
        if (!mod) return (PFN_CreateDXGIFactory1)nullptr;
        return reinterpret_cast<PFN_CreateDXGIFactory1>(GetProcAddress(mod, "CreateDXGIFactory1"));
    }();
    return fn;
}

PFN_DCompositionCreateDevice LoadDCompositionCreateDevice(){
    static PFN_DCompositionCreateDevice fn = [](){
        HMODULE mod = LoadLibraryW(L"dcomp.dll");
        if (!mod) return (PFN_DCompositionCreateDevice)nullptr;
        return reinterpret_cast<PFN_DCompositionCreateDevice>(GetProcAddress(mod, "DCompositionCreateDevice"));
    }();
    return fn;
}
}

bool GPU::initDirectCompositionPresenter(){
    directCompositionActive = false;
    destroyDirectCompositionPresenter();

    if (!wantsTransparentSwapchain()) return false;
    uint32_t pixelWidth = width;
    uint32_t pixelHeight = height;
    queryWindowPixelSize(pixelWidth, pixelHeight);
    if (pixelWidth == 0) pixelWidth = width;
    if (pixelHeight == 0) pixelHeight = height;
    width = pixelWidth;
    height = pixelHeight;

    auto d3dCreate = LoadD3D11CreateDevice();
    auto factoryCreate = LoadCreateDXGIFactory1();
    auto dcompCreate = LoadDCompositionCreateDevice();
    if (!d3dCreate || !factoryCreate || !dcompCreate) {
        return false;
    }

    IDXGIFactory2* factory = nullptr;
    if (FAILED(factoryCreate(IID_PPV_ARGS(&factory)))) {
        return false;
    }
    IDXGIAdapter1* adapter = pickDxgiAdapter(factory);

    ID3D11Device* baseDevice = nullptr;
    ID3D11DeviceContext* baseContext = nullptr;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL outLevel = D3D_FEATURE_LEVEL_11_0;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    const D3D_DRIVER_TYPE driverType = adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
    if (FAILED(d3dCreate(adapter, driverType, nullptr, flags,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
            &baseDevice, &outLevel, &baseContext))){
        SafeRelease(adapter);
        SafeRelease(factory);
        return false;
    }
    SafeRelease(adapter);
    if (FAILED(baseDevice->QueryInterface(IID_PPV_ARGS(&d3dPresenter.d3dDevice))) ||
        FAILED(baseContext->QueryInterface(IID_PPV_ARGS(&d3dPresenter.d3dContext)))){
        SafeRelease(baseDevice);
        SafeRelease(baseContext);
        SafeRelease(factory);
        return false;
    }
    SafeRelease(baseDevice);
    SafeRelease(baseContext);

    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = width;
    swapDesc.Height = height;
    swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    IDXGISwapChain1* swapChain = nullptr;
    if (FAILED(factory->CreateSwapChainForComposition(d3dPresenter.d3dDevice, &swapDesc, nullptr, &swapChain))){
        SafeRelease(factory);
        destroyDirectCompositionPresenter();
        return false;
    }

    IDCompositionDevice* dcompDevice = nullptr;
    if (FAILED(dcompCreate(nullptr, IID_PPV_ARGS(&dcompDevice)))){
        SafeRelease(factory);
        SafeRelease(swapChain);
        destroyDirectCompositionPresenter();
        return false;
    }
    IDCompositionTarget* target = nullptr;
    if (FAILED(dcompDevice->CreateTargetForHwnd(static_cast<HWND>(win32WindowHandle), TRUE, &target))){
        SafeRelease(factory);
        SafeRelease(swapChain);
        SafeRelease(dcompDevice);
        destroyDirectCompositionPresenter();
        return false;
    }
    IDCompositionVisual* visual = nullptr;
    if (FAILED(dcompDevice->CreateVisual(&visual))){
        SafeRelease(factory);
        SafeRelease(swapChain);
        SafeRelease(dcompDevice);
        SafeRelease(target);
        destroyDirectCompositionPresenter();
        return false;
    }
    visual->SetContent(swapChain);
    target->SetRoot(visual);
    dcompDevice->Commit();

    d3dPresenter.dxgiFactory = factory;
    d3dPresenter.swapChain = swapChain;
    d3dPresenter.dcompDevice = dcompDevice;
    d3dPresenter.dcompTarget = target;
    d3dPresenter.dcompVisual = visual;
    d3dPresenter.bufferCount = swapDesc.BufferCount;
    clearDirectCompositionSwapChain();
    directCompositionActive = true;
    return true;
}

void GPU::destroyDirectCompositionPresenter(){
    destroySharedRenderTargets();
    SafeRelease(d3dPresenter.dcompVisual);
    SafeRelease(d3dPresenter.dcompTarget);
    SafeRelease(d3dPresenter.dcompDevice);
    SafeRelease(d3dPresenter.swapChain);
    SafeRelease(d3dPresenter.d3dContext);
    SafeRelease(d3dPresenter.d3dDevice);
    SafeRelease(d3dPresenter.dxgiFactory);
    d3dPresenter.bufferCount = 0;
    directCompositionActive = false;
}

bool GPU::createSharedRenderTargets(uint32_t imageCount){
    destroySharedRenderTargets();
    if (!directCompositionActive) return false;
    if (imageCount == 0) imageCount = 2;
    VkExternalMemoryHandleTypeFlagBits handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
    bool requiresDedicated = false;
    if (!selectDirectCompositionHandleType(handleType, requiresDedicated)){
        return false;
    }
    directImages.resize(imageCount);
    swapchainImages.resize(imageCount);
    swapchainImageViews.resize(imageCount);
    framebuffer.assign(imageCount, VK_NULL_HANDLE);

    for (uint32_t i = 0; i < imageCount; i++){
        ID3D11Texture2D* texture = nullptr;
        D3D11_TEXTURE2D_DESC texDesc{};
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        if (handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT){
            texDesc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        } else if (handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT){
            texDesc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
        }
        if (FAILED(d3dPresenter.d3dDevice->CreateTexture2D(&texDesc, nullptr, &texture))){
            destroySharedRenderTargets();
            return false;
        }
        directImages[i].d3dTexture = texture;

        HANDLE sharedHandle = NULL;
        if (!createSharedHandleForTexture(texture, handleType, sharedHandle)){
            destroySharedRenderTargets();
            return false;
        }
        directImages[i].sharedHandle = sharedHandle;

        IDXGIKeyedMutex* keyedMutex = nullptr;
        if (FAILED(texture->QueryInterface(IID_PPV_ARGS(&keyedMutex)))){
            destroySharedRenderTargets();
            return false;
        }
        directImages[i].keyedMutex = keyedMutex;

        VkExternalMemoryImageCreateInfo externalInfo{};
        externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        externalInfo.handleTypes = handleType;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.pNext = &externalInfo;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = swapchainFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(device, &imageInfo, nullptr, &directImages[i].image) != VK_SUCCESS){
            destroySharedRenderTargets();
            return false;
        }

        VkMemoryRequirements memReqs{};
        vkGetImageMemoryRequirements(device, directImages[i].image, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkMemoryDedicatedAllocateInfo dedicatedInfo{};
        if (requiresDedicated){
            dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
            dedicatedInfo.image = directImages[i].image;
            allocInfo.pNext = &dedicatedInfo;
        }
        VkImportMemoryWin32HandleInfoKHR importInfo{};
        importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
        importInfo.handleType = handleType;
        importInfo.handle = sharedHandle;
        if (allocInfo.pNext != nullptr){
            importInfo.pNext = allocInfo.pNext;
        }
        allocInfo.pNext = &importInfo;

        if (vkAllocateMemory(device, &allocInfo, nullptr, &directImages[i].memory) != VK_SUCCESS){
            destroySharedRenderTargets();
            return false;
        }
        vkBindImageMemory(device, directImages[i].image, directImages[i].memory, 0);
        CloseHandle(sharedHandle);
        directImages[i].sharedHandle = NULL;

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = directImages[i].image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &directImages[i].view) != VK_SUCCESS){
            destroySharedRenderTargets();
            return false;
        }

        swapchainImages[i] = directImages[i].image;
        swapchainImageViews[i] = directImages[i].view;

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderpass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &swapchainImageViews[i];
        fbInfo.width = width;
        fbInfo.height = height;
        fbInfo.layers = 1;
        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffer[i]) != VK_SUCCESS){
            destroySharedRenderTargets();
            return false;
        }

        if (directImages[i].keyedMutex){
            directImages[i].keyedMutex->ReleaseSync(0);
        }
    }
    clearDirectCompositionImages();
    return true;
}

void GPU::clearDirectCompositionSwapChain(){
    if (!d3dPresenter.swapChain || !d3dPresenter.d3dDevice || !d3dPresenter.d3dContext){
        return;
    }
    const FLOAT transparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (UINT bufferIndex = 0; bufferIndex < d3dPresenter.bufferCount; ++bufferIndex){
        ID3D11Texture2D* buffer = nullptr;
        if (FAILED(d3dPresenter.swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&buffer)))){
            SafeRelease(buffer);
            continue;
        }
        ID3D11RenderTargetView* rtv = nullptr;
        if (SUCCEEDED(d3dPresenter.d3dDevice->CreateRenderTargetView(buffer, nullptr, &rtv))){
            d3dPresenter.d3dContext->ClearRenderTargetView(rtv, transparent);
            rtv->Release();
        }
        SafeRelease(buffer);
    }
    d3dPresenter.d3dContext->Flush();
}

void GPU::clearDirectCompositionImages(){
    if (directImages.empty()) return;
    if (device == VK_NULL_HANDLE || queue == VK_NULL_HANDLE) return;
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool tempPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &tempPool) != VK_SUCCESS){
        return;
    }
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = tempPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &allocInfo, &cmd) != VK_SUCCESS){
        vkDestroyCommandPool(device, tempPool, nullptr);
        return;
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS){
        vkFreeCommandBuffers(device, tempPool, 1, &cmd);
        vkDestroyCommandPool(device, tempPool, nullptr);
        return;
    }
    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    for (size_t i = 0; i < framebuffer.size(); ++i){
        if (framebuffer[i] == VK_NULL_HANDLE) continue;
        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = renderpass;
        rpInfo.framebuffer = framebuffer[i];
        rpInfo.renderArea.offset = {0, 0};
        rpInfo.renderArea.extent = {width, height};
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &clearValue;
        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(cmd);
    }
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, tempPool, 1, &cmd);
    vkDestroyCommandPool(device, tempPool, nullptr);
}

void GPU::destroySharedRenderTargets(){
    for (auto& fb : framebuffer){
        if (fb != VK_NULL_HANDLE){
            vkDestroyFramebuffer(device, fb, nullptr);
            fb = VK_NULL_HANDLE;
        }
    }
    for (auto& img : directImages){
        if (img.d3dTexture){
            img.d3dTexture->Release();
            img.d3dTexture = nullptr;
        }
        if (img.keyedMutex){
            img.keyedMutex->Release();
            img.keyedMutex = nullptr;
        }
        if (img.sharedHandle){
            CloseHandle(img.sharedHandle);
            img.sharedHandle = NULL;
        }
        if (img.view != VK_NULL_HANDLE){
            vkDestroyImageView(device, img.view, nullptr);
            img.view = VK_NULL_HANDLE;
        }
        if (img.image != VK_NULL_HANDLE){
            vkDestroyImage(device, img.image, nullptr);
            img.image = VK_NULL_HANDLE;
        }
        if (img.memory != VK_NULL_HANDLE){
            vkFreeMemory(device, img.memory, nullptr);
            img.memory = VK_NULL_HANDLE;
        }
    }
    directImages.clear();
    swapchainImages.clear();
    swapchainImageViews.clear();
    framebuffer.clear();
}

void GPU::presentWithDirectComposition(uint32_t imageIndex){
    if (!directCompositionActive) return;
    if (imageIndex >= directImages.size()) return;
    if (!d3dPresenter.swapChain || !d3dPresenter.d3dContext) return;
    ID3D11Texture2D* sharedTexture = directImages[imageIndex].d3dTexture;
    if (!sharedTexture) return;
    IDXGIKeyedMutex* keyedMutex = directImages[imageIndex].keyedMutex;
    if (keyedMutex){
        if (FAILED(keyedMutex->AcquireSync(1, INFINITE))){
            return;
        }
    }

    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(d3dPresenter.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))){
        d3dPresenter.d3dContext->CopyResource(backBuffer, sharedTexture);
    }
    SafeRelease(backBuffer);
    d3dPresenter.swapChain->Present(1, 0);
    static uint32_t frameCounter = 0;
    if (frameCounter < 5){
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "presented image %u", imageIndex);
        frameCounter++;
    }
    if (keyedMutex){
        keyedMutex->ReleaseSync(0);
    }
}

void GPU::configureTransparentWindow(){
    if (!wantsTransparentSwapchain()) return;
    if (window == NULL) return;
    SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    if (properties == 0) return;
    void* hwndProperty = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (hwndProperty == NULL) return;
    HWND hwnd = static_cast<HWND>(hwndProperty);
    win32WindowHandle = hwnd;

    const LONG_PTR existingStyles = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if ((existingStyles & WS_EX_LAYERED) == 0) {
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, existingStyles | WS_EX_LAYERED);
    }
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != NULL) {
        auto setWindowCompositionAttribute = reinterpret_cast<SetWindowCompositionAttributeFn>(
                GetProcAddress(user32, "SetWindowCompositionAttribute"));
        if (setWindowCompositionAttribute != NULL) {
            PhotonAccentPolicy policy{};
            policy.accentState = static_cast<DWORD>(PhotonAccentState::BlurBehind);
            policy.accentFlags = 0;
            policy.gradientColor = 0;
            policy.animationId = 0;

            PhotonWindowCompositionAttribData data{};
            data.attribute = PhotonWindowCompositionAttrib::AccentPolicy;
            data.data = &policy;
            data.dataSize = sizeof(policy);

            setWindowCompositionAttribute(hwnd, &data);
        }
    }

    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (dwmapi != NULL) {
        auto dwmExtendFrame = reinterpret_cast<DwmExtendFrameIntoClientAreaFn>(
                GetProcAddress(dwmapi, "DwmExtendFrameIntoClientArea"));
        auto dwmEnableBlur = reinterpret_cast<DwmEnableBlurBehindWindowFn>(
                GetProcAddress(dwmapi, "DwmEnableBlurBehindWindow"));
        if (dwmExtendFrame != NULL) {
            MARGINS margins{ -1, -1, -1, -1 };
            dwmExtendFrame(hwnd, &margins);
        }
        if (dwmEnableBlur != NULL) {
            HRGN blurRegion = CreateRectRgn(0, 0, -1, -1);
            DWM_BLURBEHIND blur{};
            blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
            blur.fEnable = TRUE;
            blur.hRgnBlur = blurRegion;
            blur.fTransitionOnMaximized = FALSE;
            dwmEnableBlur(hwnd, &blur);
            if (blurRegion != NULL) {
                DeleteObject(blurRegion);
            }
        }
        FreeLibrary(dwmapi);
    }
}

#else
SDL_Window* GPU::createWindow(){
    return SDL_CreateWindow("Photon", width, height, 
        SDL_WINDOW_VULKAN | SDL_WINDOW_TRANSPARENT | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
}

VkCompositeAlphaFlagBitsKHR GPU::pickCompositeAlpha(const VkSurfaceCapabilitiesKHR& surfaceCapabilities){
    const bool transparent = wantsTransparentSwapchain();
    const VkCompositeAlphaFlagBitsKHR transparentOrder[] = {
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    const VkCompositeAlphaFlagBitsKHR opaqueOrder[] = { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
    if(transparent)
        for(const auto mode : transparentOrder)
            if ((surfaceCapabilities.supportedCompositeAlpha & mode) != 0)
                return mode;

    for(const auto mode : opaqueOrder)
        if((surfaceCapabilities.supportedCompositeAlpha & mode) != 0)
            return mode;

    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

#endif
#endif
