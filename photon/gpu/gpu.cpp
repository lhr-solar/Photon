#include <SDL3/SDL_oldnames.h>
#include <array>
#include <cstring>
#include <vector>

#include <SDL3/SDL.h>
#include <vulkan_core.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include "ui_frag_spv.hpp"
#include "ui_vert_spv.hpp"
#include "imgui.h"

#include "gpu.hpp"

void GPU::init() {
    uint32_t count = 0;
    std::vector<const char *> enabledLayers{};
    std::vector<VkExtensionProperties> extensionProperties{};
    std::vector<VkLayerProperties> layerProperties{};
    std::vector<VkPhysicalDevice> physicalDevices{};
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};
    std::vector<const char*> deviceExtensions{};
    std::vector<VkSurfaceFormatKHR> surfaceFormats{};
    std::vector<VkPresentModeKHR> presentModes{};
    vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    extensionProperties.resize(count);
    vkEnumerateInstanceExtensionProperties(NULL, &count, extensionProperties.data());
    vkEnumerateInstanceLayerProperties(&count, NULL);
    layerProperties.resize(count);
    vkEnumerateInstanceLayerProperties(&count, layerProperties.data());

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Vulkan_LoadLibrary(NULL);
    window = SDL_CreateWindow("Photon", width, height, SDL_WINDOW_VULKAN);
    const char *const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&count);
    std::vector<const char *> enabledExtensions(sdlExtensions, sdlExtensions + count);
    enabledExtensions.push_back( VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
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

    VkSurfaceCapabilitiesKHR surfaceCapabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);

    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, NULL);
    surfaceFormats.resize(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, surfaceFormats.data());
    swapchainFormat = surfaceFormats[0].format; swapchainColorspace = surfaceFormats[0].colorSpace;
    for(const auto& f : surfaceFormats){
        if(f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){
            swapchainFormat = f.format;
            swapchainColorspace = f.colorSpace;
            break;
        }
    }
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, NULL);
    presentModes.resize(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, presentModes.data());
    presentationMode = VK_PRESENT_MODE_FIFO_KHR;
    for(const auto& p : presentModes)
        if(p == VK_PRESENT_MODE_MAILBOX_KHR) presentationMode = p;

    VkSwapchainCreateInfoKHR swapchainCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .surface = surface,
        .minImageCount = surfaceCapabilities.minImageCount,
        .imageFormat = swapchainFormat,
        .imageColorSpace = swapchainColorspace,
        .imageExtent = {width, height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentationMode,
        .clipped = VK_FALSE,
        .oldSwapchain = NULL
    }; vkCreateSwapchainKHR(device, &swapchainCreateInfo, NULL, &swapchain);
    vkGetSwapchainImagesKHR(device, swapchain, &count, NULL);
    swapchainImages.resize(count);
    vkGetSwapchainImagesKHR(device, swapchain, &count, swapchainImages.data());
    swapchainImageViews.resize(swapchainImages.size());
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
        }; vkCreateImageView(device, &imageViewCreateInfo, NULL, &swapchainImageViews[i]);
    }
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
    framebuffer.resize(swapchainImages.size());
    for(int i = 0; i < swapchainImages.size(); i++){
        std::vector<VkImageView> imageViews = {swapchainImageViews[i]};
        VkFramebufferCreateInfo framebufferCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .renderPass = renderpass,
            .attachmentCount = static_cast<uint32_t>(imageViews.size()),
            .pAttachments = imageViews.data(),
            .width = width,
            .height = height,
            .layers = 1,
        }; vkCreateFramebuffer(device, &framebufferCreateInfo , NULL, &framebuffer[i]);
    }

    renderCompleteSemaphores.resize(swapchainImages.size());
    VkSemaphoreCreateInfo sCI = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for(auto& s : renderCompleteSemaphores) vkCreateSemaphore(device, &sCI, NULL, &s);

    imageAvailableSemaphores.resize(swapchainImages.size());
    for(auto& s : imageAvailableSemaphores) vkCreateSemaphore(device, &sCI, NULL, &s);

    VkFenceCreateInfo fCI = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    fences.resize(swapchainImages.size());
    for(auto& f : fences) vkCreateFence(device, &fCI, NULL, &f);

    VkCommandPoolCreateInfo commandPoolCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilyIndex,
    }; vkCreateCommandPool(device, &commandPoolCreateInfo, NULL, &commandPool);
    commandBuffers.resize(swapchainImages.size());
    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(commandBuffers.size()),
    }; vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, commandBuffers.data());
};

void GPU::imguiBackend(){
    ImGui::CreateContext();
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
    blendAttachmentState.srcAlphaBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
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
    clearValues[0].color = {{0.0f, 0.00f, 0.00f, 1.0f}};
    VkRenderPassBeginInfo renderPassBeginInfo {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderpass;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width = width;
    renderPassBeginInfo.renderArea.extent.height = height;
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = clearValues;

    ImGui::NewFrame();
    ImGui::ShowDemoWindow();
    ImGui::Render();

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

    // Upload data
    ImDrawVert *vtxDst = (ImDrawVert *)vertexBufferMapped[frameIndex];
    ImDrawIdx *idxDst = (ImDrawIdx *)indexBufferMapped[frameIndex];

    for (int n = 0; n < imDrawData->CmdListsCount; n++) {
      const ImDrawList *cmd_list = imDrawData->CmdLists[n];
      memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
      memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
      vtxDst += cmd_list->VtxBuffer.Size;
      idxDst += cmd_list->IdxBuffer.Size;
    }
    // end of update buffers

    VkCommandBufferBeginInfo cmdBufferBeginInfo {};
    cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VkCommandBuffer& commandBuffer = commandBuffers[frameIndex];
    renderPassBeginInfo.framebuffer = framebuffer[imgIdx];
    vkResetCommandBuffer(commandBuffer, 0);
    vkBeginCommandBuffer(commandBuffer, &cmdBufferBeginInfo);
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
    if (displaySize.x <= 0.0f || displaySize.y <= 0.0f) {
        vkCmdEndRenderPass(commandBuffer);
        vkEndCommandBuffer(commandBuffer);
        return;
    }
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
                if (pcmd->GetTexID() != 0) {
                    textureSet = reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(pcmd->GetTexID()));
                }
                if (textureSet == VK_NULL_HANDLE) {
                    textureSet = descriptorSet;
                }
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
                if (scissorRect.extent.width == 0 || scissorRect.extent.height == 0) {
                    continue;
                }
                vkCmdSetScissor(commandBuffer, 0, 1, &scissorRect);
                vkCmdDrawIndexed(commandBuffer,
                        pcmd->ElemCount,
                        1,
                        globalIndexOffset + static_cast<uint32_t>(pcmd->IdxOffset),
                        globalVertexOffset + static_cast<int32_t>(pcmd->VtxOffset),
                        0);
            }
            globalIndexOffset += static_cast<uint32_t>(cmd_list->IdxBuffer.Size);
            globalVertexOffset += cmd_list->VtxBuffer.Size;
        }
    }
    vkCmdEndRenderPass(commandBuffer);
    vkEndCommandBuffer(commandBuffer);
};

void GPU::startFrame(uint32_t& imgIdx){
    vkWaitForFences(device, 1, &fences[frameIndex], VK_TRUE, UINT64_MAX);
    vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, 
        imageAvailableSemaphores[frameIndex], VK_NULL_HANDLE, &imgIdx);
    vkResetFences(device, 1, &fences[frameIndex]);
};

void GPU::submitFrame(const uint32_t imgIdx){
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = 0,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &imageAvailableSemaphores[frameIndex],
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffers[frameIndex],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderCompleteSemaphores[imgIdx], 
    };
    vkQueueSubmit(queue, 1, &submitInfo, fences[frameIndex]);
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
    vkQueuePresentKHR(queue, &presentInfo);
}

void GPU::destroy() {
    vkDeviceWaitIdle(device);
    SDL_DestroyWindow(window);
    ImGui::DestroyContext();
    for(int i = 0; i < swapchainImages.size(); i++){
        vkDestroyImageView(device, swapchainImageViews[i], NULL);
        vkDestroyFramebuffer(device, framebuffer[i], NULL);
        vkDestroySemaphore(device, renderCompleteSemaphores[i], NULL);
        vkDestroySemaphore(device, imageAvailableSemaphores[i], NULL);
        vkDestroyFence(device, fences[i], NULL);
        vkDestroyBuffer(device, vertexBuffers[i], NULL);
        vkDestroyBuffer(device, indexBuffers[i], NULL);
        vkUnmapMemory(device, vertexBufferMemories[i]);
        vkUnmapMemory(device, indexBufferMemories[i]);
        vkFreeMemory(device, vertexBufferMemories[i], NULL);
        vkFreeMemory(device, indexBufferMemories[i], NULL);
    }
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyShaderModule(device, uiShaderVert, NULL);
    vkDestroyShaderModule(device, uiShaderIndex, NULL);
    vkDestroyImage(device, fontImage, NULL);
    vkDestroyImageView(device, fontView, NULL);
    vkDestroySampler(device, fontSampler, NULL);
    vkFreeMemory(device, fontMemory, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyRenderPass(device, renderpass, NULL);
    vkFreeCommandBuffers(device, commandPool, commandBuffers.size(), commandBuffers.data());
    vkDestroyCommandPool(device, commandPool, NULL);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, NULL);
    vkFreeDescriptorSets(device, descriptorPool, 1, &descriptorSet);
    vkDestroyDescriptorPool(device, descriptorPool, NULL);
    vkDestroyPipeline(device, imguiPipeline, NULL);
    vkDestroyPipelineLayout(device, imguiPipelineLayout, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
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

VkPipelineShaderStageCreateInfo GPU::loadShader(const uint32_t* code, size_t size, VkShaderModule& module, VkShaderStageFlagBits flagBits, VkDevice device){
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
    printf("validation layer: %s \n", pCallbackData->pMessage);
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
