/*[π] the photon gpu interface*/
#include <algorithm>
#include <assert.h>
#include <array>
#include <string.h>

#include "vulkan/vulkan_core.h"
#include "gpu.hpp"
//#include "vulkanGLTF.hpp"
#include "../engine/include.hpp"
#include "vulkanShader.hpp"

#include "scene_frag_spv.hpp"
#include "scene_vert_spv.hpp"

bool Gpu::initVulkan(){
    VkResult result;

    result = createInstance();
    if (result != VK_SUCCESS)
        fatal("[!] Failed to initialize vulkan", result);

    result = setupVulkanDevice();
    if(result != VK_SUCCESS)
        fatal("[!] Failed to setup GPU", result);

    VkBool32 validFormat {false};
    validFormat = getSupportedDepthFormat(vulkanDevice.physicalDevice, &depthFormat);
    logs("[+] Using Depth Format : " << depthFormat);
    assert(validFormat);

    return true;
}

void Gpu::getValidationLayerSupport(){
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);
    supportedInstanceLayers.resize(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, supportedInstanceLayers.data());
    logs("[?] Available Vulkan Layers");
    bool found = false;
    for(auto layer : supportedInstanceLayers){
        logs("[+] " << layer.layerName); 
        if(strcmp(validationLayer, layer.layerName) == 0){
            found = true;
        }
    }
    if(found) enabledInstanceLayers.push_back("VK_LAYER_KHRONOS_validation");
}

VkResult Gpu::createInstance(){
    getValidationLayerSupport();
    enabledInstanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    std::vector<const char*> instanceExtensions = {VK_KHR_SURFACE_EXTENSION_NAME};
#ifdef XCB
    instanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif
#ifdef _WIN32
    instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    if(extCount > 0){
        std::vector<VkExtensionProperties> extensions(extCount);
        if (vkEnumerateInstanceExtensionProperties(nullptr, &extCount, &extensions.front()) == VK_SUCCESS){
            logs("[?] Available Vulkan Instance Extensions:");
            for (VkExtensionProperties& extension : extensions){
                supportedInstanceExtensions.push_back(extension.extensionName);
                logs("[+] " << extension.extensionName);
            }
        }
    }

    if(enabledInstanceExtensions.size() > 0){
        for(const char* enabledExtension : enabledInstanceExtensions){
            if(std::find(supportedInstanceExtensions.begin(), supportedInstanceExtensions.end(), enabledExtension) 
                    == supportedInstanceExtensions.end())
                logs("[!] Enabled Instance Extension \"" << enabledExtension << "\" is not present at instance level");
            instanceExtensions.push_back(enabledExtension);
        }
    }

    VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = name.c_str();
	appInfo.pEngineName = name.c_str();
	appInfo.apiVersion = apiVersion;

	VkInstanceCreateInfo instanceCreateInfo{};
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.pApplicationInfo = &appInfo;

    if (instanceExtensions.size() > 0) {
		instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
		instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
	}

    logs("[?] Enabled Vulkan Instance Extensions:");
    for(int i = 0; i < instanceCreateInfo.enabledExtensionCount; i++){
        logs("[+] " << instanceCreateInfo.ppEnabledExtensionNames[i]);
    }

    VK_CHECK(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));

    return VK_SUCCESS;
}

VkResult Gpu::setupVulkanDevice(){
    logs("[+] Constructing GPU");
    uint32_t gpuCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr));
    if(gpuCount == 0)
        fatal("[!] Could not enumerate physical devices", -1);

    physicalDevices.resize(gpuCount);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &gpuCount, physicalDevices.data()));

    // select first device by default, may want to expand this in the future
    uint32_t selectedDevice = 0;
    VkPhysicalDevice physicalDevice = physicalDevices[selectedDevice];
    vulkanDevice.initDevice(physicalDevice);

    // TODO if we want to run headless, add interfaces for these, also consider your queues and extensions
    requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT; 
    // TODO if we want a dedicated transfer queue + better queue selection, improve the queue selection in the following
    VK_CHECK(vulkanDevice.createLogicalDevice(vulkanDevice.enabledFeatures, vulkanDevice.enabledDeviceExtensions, nullptr, requestedQueueTypes));

    vkGetDeviceQueue(vulkanDevice.logicalDevice, vulkanDevice.queueFamilyIndices.graphics, 0, &vulkanDevice.graphicsQueue);
    vkGetDeviceQueue(vulkanDevice.logicalDevice, vulkanDevice.queueFamilyIndices.compute, 1, &vulkanDevice.computeQueue);
    return VK_SUCCESS;
}

void Gpu::createSynchronizationPrimitives(VkDevice device, std::vector<VkCommandBuffer> drawCmdBuffers){
    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    waitFences.resize(drawCmdBuffers.size());
    for(auto& fence : waitFences){
        vkCreateFence(device, &fenceCreateInfo, nullptr, &fence);
    }
    logs("[+] Created " << waitFences.size() << " Synchronization Primitives");

    VkSemaphoreCreateInfo semaphoreCreateInfo {};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(vulkanDevice.logicalDevice, &semaphoreCreateInfo, nullptr, &semaphores.presentComplete));
    VK_CHECK(vkCreateSemaphore(vulkanDevice.logicalDevice, &semaphoreCreateInfo, nullptr, &semaphores.renderComplete));
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pWaitDstStageMask = &submitPipelineStages;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &semaphores.presentComplete;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &semaphores.renderComplete;
}

VkBool32 Gpu::getSupportedDepthStencilFormat(VkPhysicalDevice physicalDevice, VkFormat* depthStencilFormat){
    std::vector<VkFormat> formatList = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
    };

    for (auto& format : formatList) {
        VkFormatProperties formatProps;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            *depthStencilFormat = format;
            return true;
        }
    }

    return false;
}

VkBool32 Gpu::getSupportedDepthFormat(VkPhysicalDevice physicalDevice, VkFormat* depthFormat){
    std::vector<VkFormat> formatList = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };

    for (auto& format : formatList) {
        VkFormatProperties formatProps;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            *depthFormat = format;
            return true;
        }
    }
    
    return false;
}

void Gpu::setupDepthStencil(uint32_t width, uint32_t height){
    VkImageCreateInfo imageCI{}; imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = depthFormat;
	imageCI.extent = { width, height, 1 };
	imageCI.mipLevels = 1;
	imageCI.arrayLayers = 1;
	imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VK_CHECK(vkCreateImage(vulkanDevice.logicalDevice, &imageCI, nullptr, &depthStencil.image));
    logs("[+] Created Depth Stencil Image");

    VkMemoryRequirements memReqs{};
	vkGetImageMemoryRequirements(vulkanDevice.logicalDevice, depthStencil.image, &memReqs);

    VkMemoryAllocateInfo memAlloc{};
	memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = vulkanDevice.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr);
    VK_CHECK(vkAllocateMemory(vulkanDevice.logicalDevice, &memAlloc, nullptr, &depthStencil.memory));
    VK_CHECK(vkBindImageMemory(vulkanDevice.logicalDevice, depthStencil.image, depthStencil.memory, 0));
    logs("[+] Allocated Depth Stencil Memory of size : " << memReqs.size);

    VkImageViewCreateInfo imageViewCI{};
    imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCI.image = depthStencil.image;
    imageViewCI.format = depthFormat;
    imageViewCI.subresourceRange.baseMipLevel = 0;
    imageViewCI.subresourceRange.levelCount = 1;
	imageViewCI.subresourceRange.baseArrayLayer = 0;
	imageViewCI.subresourceRange.layerCount = 1;
	imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	// Stencil aspect should only be set on depth + stencil formats (VK_FORMAT_D16_UNORM_S8_UINT..VK_FORMAT_D32_SFLOAT_S8_UINT
	if (depthFormat >= VK_FORMAT_D16_UNORM_S8_UINT) {
		imageViewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	VK_CHECK(vkCreateImageView(vulkanDevice.logicalDevice, &imageViewCI, nullptr, &depthStencil.view));
    logs("[+] Created Depth Stencil Image View ");
};

void Gpu::setupRenderPass(VkDevice device, VkSurfaceFormatKHR surfaceFormat){
    std::array<VkAttachmentDescription, 2> attachments = {};
    // Color attachment
    attachments[0].format = surfaceFormat.format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    // Depth attachment
    attachments[1].format = depthFormat;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference = {};
	colorReference.attachment = 0;
	colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference = {};
	depthReference.attachment = 1;
	depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassDescription = {};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;
	subpassDescription.pDepthStencilAttachment = &depthReference;
	subpassDescription.inputAttachmentCount = 0;
	subpassDescription.pInputAttachments = nullptr;
	subpassDescription.preserveAttachmentCount = 0;
	subpassDescription.pPreserveAttachments = nullptr;
	subpassDescription.pResolveAttachments = nullptr;

    // Subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 2> dependencies{};

	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	dependencies[0].dependencyFlags = 0;

	dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].dstSubpass = 0;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].srcAccessMask = 0;
	dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	dependencies[1].dependencyFlags = 0;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

    VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));

    logs("[+] Created Render Pass ");
}

void Gpu::setupFrameBuffer(VkDevice device, std::vector<SwapChainBuffer> swapChainBuffers, uint32_t imageCount, uint32_t width, uint32_t height){
    frameBuffers.resize(imageCount);
	for (uint32_t i = 0; i < frameBuffers.size(); i++)
	{
		const VkImageView attachments[2] = {
			swapChainBuffers[i].view,
			depthStencil.view
		};
		VkFramebufferCreateInfo frameBufferCreateInfo{};
		frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferCreateInfo.renderPass = renderPass;
		frameBufferCreateInfo.attachmentCount = 2;
		frameBufferCreateInfo.pAttachments = attachments;
		frameBufferCreateInfo.width = width;
		frameBufferCreateInfo.height = height;
		frameBufferCreateInfo.layers = 1;
		VK_CHECK(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &frameBuffers[i]));
        logs("[+] Created Frame Buffer for Image : " << i);
	}
}

void Gpu::prepareUniformBuffers(){
    // Vertex shader uniform buffer block
    VK_CHECK(vulkanDevice.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
        &uniformBufferVS, sizeof(uboVS), &uboVS));
}

void Gpu::updateUniformBuffers(bool animateLight, float lightTimer, float lightSpeed){
    // TODO: think about this _./~\._
    // Vertex shader
    uboVS.projection = camera.matrices.perspective;
    uboVS.modelView  = camera.matrices.view * glm::mat4(1.0f);
    // Light source
    if (animateLight) {
      lightTimer += frameTime * lightSpeed;
      uboVS.lightPos.x =
          sin(glm::radians(lightTimer * 360.0f)) * 15.0f;
      uboVS.lightPos.z =
          cos(glm::radians(lightTimer * 360.0f)) * 15.0f;
    };
    VK_CHECK(uniformBufferVS.map(VK_WHOLE_SIZE, 0));
    memcpy(uniformBufferVS.mapped, &uboVS, sizeof(uboVS));
    uniformBufferVS.unmap();
}

// TODO: make this modular
void Gpu::setupDescriptors(VkDevice device){
    VkDescriptorPoolSize descriptorPoolSize {};
    descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorPoolSize.descriptorCount = 8;
    std::vector<VkDescriptorPoolSize> poolSizes = { descriptorPoolSize };

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolInfo.pPoolSizes = poolSizes.data();
    descriptorPoolInfo.maxSets = 8;

    VK_CHECK(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    logs("[+] Created Descriptor Pool of count " << descriptorPoolInfo.poolSizeCount);

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
    VK_CHECK(vkCreateDescriptorSetLayout(vulkanDevice.logicalDevice, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout));

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo {};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = descriptorPool;
    descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    VK_CHECK(vkAllocateDescriptorSets(vulkanDevice.logicalDevice, &descriptorSetAllocateInfo, &descriptorSet));
}

void Gpu::preparePipelines(VkDevice device){
}

void Gpu::setImageLayout( VkCommandBuffer cmdbuffer, VkImage image, VkImageLayout oldImageLayout, VkImageLayout newImageLayout, 
                          VkImageSubresourceRange subresourceRange, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask){
    // Create an image barrier object
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

    vkCmdPipelineBarrier( cmdbuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
}
