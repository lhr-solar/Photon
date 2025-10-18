/*[π] the photon gpu interface*/
#include <algorithm>
#include <assert.h>
#include <array>
#include <string.h>

#include "vulkan/vulkan_core.h"
#include "gpu.hpp"
#include "vulkanGLTF.hpp"
#include "../engine/include.hpp"
#include "../gui/inputs.hpp"
#include "imgui.h"
#include "tiny_gltf.h"
#include "scene_frag_spv.hpp"
#include "scene_vert_spv.hpp"

namespace tinygltf
{
    class Model;
    struct Primitive;
    struct Node;
}

bool Gpu::initVulkan()
{
    // interface for variable extensions in the future?
    // vulkan pNext... compatability
    enabledInstanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    VkResult result = createInstance();
    if (result != VK_SUCCESS)
        fatal("[!] Failed to initialize vulkan", result);

    result = setupGPU();
    if (result != VK_SUCCESS)
        fatal("[!] Failed to setup GPU", result);

    // set stencil
    VkBool32 validFormat{false};
    if (requiresStencil)
    {
        validFormat = getSupportedDepthStencilFormat(vulkanDevice.physicalDevice, &depthFormat);
        logs("[+] Using Depth Stencil Format : " << depthFormat);
    }
    else
    {
        validFormat = getSupportedDepthFormat(vulkanDevice.physicalDevice, &depthFormat);
        logs("[+] Using Depth Format : " << depthFormat);
    }
    assert(validFormat);

    // create synch. objects
    VkSemaphoreCreateInfo semaphoreCreateInfo{};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    // ensure image is displayed before we start submitting new commands
    VK_CHECK(vkCreateSemaphore(vulkanDevice.logicalDevice, &semaphoreCreateInfo, nullptr, &semaphores.presentComplete));
    // ensure image is not presented until we have submitted and executed all commands
    VK_CHECK(vkCreateSemaphore(vulkanDevice.logicalDevice, &semaphoreCreateInfo, nullptr, &semaphores.renderComplete));

    // submit info only valid for graphics atm
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pWaitDstStageMask = &submitPipelineStages;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &semaphores.presentComplete;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &semaphores.renderComplete;

    return true;
}

VkResult Gpu::createInstance()
{
    std::vector<const char *> instanceExtensions = {VK_KHR_SURFACE_EXTENSION_NAME};
#ifdef XCB
    instanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif
#ifdef _WIN32
    instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
    // grab available extensions
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    if (extCount > 0)
    {
        std::vector<VkExtensionProperties> extensions(extCount);

        if (vkEnumerateInstanceExtensionProperties(nullptr, &extCount, &extensions.front()) == VK_SUCCESS)
        {
            logs("[?] Available Vulkan Instance Extensions:");
            for (VkExtensionProperties &extension : extensions)
            {
                supportedInstanceExtensions.push_back(extension.extensionName);
                logs("[+] " << extension.extensionName);
            }
        }
    }

    if (enabledInstanceExtensions.size() > 0)
    {
        for (const char *enabledExtension : enabledInstanceExtensions)
        {
            if (std::find(supportedInstanceExtensions.begin(), supportedInstanceExtensions.end(), enabledExtension) == supportedInstanceExtensions.end())
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

    if (instanceExtensions.size() > 0)
    {
		instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
		instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
	}

    logs("[?] Enabled Vulkan Instance Extensions:");
    for (int i = 0; i < instanceCreateInfo.enabledExtensionCount; i++)
    {
        logs("[+] " << instanceCreateInfo.ppEnabledExtensionNames[i]);
    }

    VK_CHECK(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));

    return VK_SUCCESS;
}

VkResult Gpu::setupGPU()
{
    logs("[+] Constructing GPU");
    uint32_t gpuCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr));
    if (gpuCount == 0)
        fatal("[!] Could not enumerate physical devices", -1);

    std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &gpuCount, physicalDevices.data()));

    // select first device by default, may want to expand this in the future
    uint32_t selectedDevice = 0;
    VkPhysicalDevice physicalDevice = physicalDevices[selectedDevice];
    vulkanDevice.initDevice(physicalDevice);

    // TODO if we want to run headless, add interfaces for these, also consider your queues and extensions
    useSwapchain = true;
    requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    // TODO if we want a dedicated transfer queue + better queue selection, improve the queue selection in the following
    VK_CHECK(vulkanDevice.createLogicalDevice(vulkanDevice.enabledFeatures, vulkanDevice.enabledDeviceExtensions, nullptr, useSwapchain, requestedQueueTypes));

    // remove index magic number, should be done programatically, see above --
    vkGetDeviceQueue(vulkanDevice.logicalDevice, vulkanDevice.queueFamilyIndices.graphics, 0, &vulkanDevice.graphicsQueue);
    vkGetDeviceQueue(vulkanDevice.logicalDevice, vulkanDevice.queueFamilyIndices.compute, 0, &vulkanDevice.computeQueue);
    vkGetDeviceQueue(vulkanDevice.logicalDevice, vulkanDevice.queueFamilyIndices.transfer, 0, &vulkanDevice.transferQueue);

    return VK_SUCCESS;
}

void Gpu::createSynchronizationPrimitives(VkDevice device, std::vector<VkCommandBuffer> drawCmdBuffers)
{
    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    waitFences.resize(drawCmdBuffers.size());
    for (auto &fence : waitFences)
    {
        vkCreateFence(device, &fenceCreateInfo, nullptr, &fence);
    }
    logs("[+] Created " << waitFences.size() << " Synchronization Primitives");
}

VkBool32 Gpu::getSupportedDepthStencilFormat(VkPhysicalDevice physicalDevice, VkFormat *depthStencilFormat)
{
    std::vector<VkFormat> formatList = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
    };

    for (auto &format : formatList)
    {
        VkFormatProperties formatProps;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            *depthStencilFormat = format;
            return true;
        }
    }

    return false;
}

VkBool32 Gpu::getSupportedDepthFormat(VkPhysicalDevice physicalDevice, VkFormat *depthFormat)
{
    std::vector<VkFormat> formatList = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM};

    for (auto &format : formatList)
    {
        VkFormatProperties formatProps;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            *depthFormat = format;
            return true;
        }
    }
    
    return false;
}

void Gpu::setupDepthStencil(uint32_t width, uint32_t height)
{
    VkImageCreateInfo imageCI{};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = depthFormat;
    imageCI.extent = {width, height, 1};
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
    if (depthFormat >= VK_FORMAT_D16_UNORM_S8_UINT)
    {
		imageViewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	VK_CHECK(vkCreateImageView(vulkanDevice.logicalDevice, &imageViewCI, nullptr, &depthStencil.view));
    logs("[+] Created Depth Stencil Image View ");
};

void Gpu::setupRenderPass(VkDevice device, VkSurfaceFormatKHR surfaceFormat)
{
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

void Gpu::createPipelineCache(VkDevice device)
{
    VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
    pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &pipelineCache);

    logs("[+] Created Pipeline Cache");
}

void Gpu::setupFrameBuffer(VkDevice device, std::vector<SwapChainBuffer> swapChainBuffers, uint32_t imageCount, uint32_t width, uint32_t height)
{
    frameBuffers.resize(imageCount);
	for (uint32_t i = 0; i < frameBuffers.size(); i++)
	{
		const VkImageView attachments[2] = {
			swapChainBuffers[i].view,
			// Depth/Stencil attachment is the same for all frame buffers
            depthStencil.view};
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

void Gpu::prepareUniformBuffers()
{
    // Initialize camera
    camera.type = Camera::CameraType::firstperson;
    camera.position = glm::vec3(0.0f, 0.0f, -10.0f);  // Move camera further back to see the model
    camera.rotation = glm::vec3(0.0f, 0.0f, 0.0f);
    camera.fov = 60.0f;
    camera.znear = 0.1f;
    camera.zfar = 100.0f;
    camera.rotationSpeed = 0.25f;
    camera.movementSpeed = 5.0f;
    camera.updateAspectRatio(1.0f);  // Will be updated with actual aspect ratio later
    camera.updateViewMatrix();
    
    // Initialize uniform buffer data
    uboVS.projection = camera.matrices.perspective;
    uboVS.model = camera.matrices.view;
    uboVS.lightPos = glm::vec4(5.0f, 5.0f, -5.0f, 1.0f);
    
    logs("[DEBUG] Camera position: " << camera.position.x << ", " << camera.position.y << ", " << camera.position.z);
    logs("[DEBUG] Camera fov: " << camera.fov << ", znear: " << camera.znear << ", zfar: " << camera.zfar);
    
    // Vertex shader uniform buffer block
    VK_CHECK(vulkanDevice.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
        &uniformBufferVS, sizeof(uboVS), &uboVS));
}

void Gpu::updateUniformBuffers(bool animateLight, float lightTimer, float lightSpeed)
{
    // TODO: think about this _./~\._
    // Vertex shader
    uboVS.projection = camera.matrices.perspective;
    uboVS.model = camera.matrices.view;
    // Light source
    if (animateLight)
    {
      lightTimer += frameTimer * lightSpeed;
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
void Gpu::setupLayoutsAndDescriptors(VkDevice device)
{
    // TODO what is all this man? look at modern examples! 
    // Descriptor Pool
    // ermm, the discriptor count increased to handle many mashes
    VkDescriptorPoolSize uniformPoolSize = {};
    uniformPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformPoolSize.descriptorCount = 10;

    VkDescriptorPoolSize imageSamplerPoolSize = {};
    imageSamplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    imageSamplerPoolSize.descriptorCount = 10;

    // storage buffer for materials
    VkDescriptorPoolSize storageBufferPoolSize = {};
    storageBufferPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    storageBufferPoolSize.descriptorCount = 20;
    std::vector<VkDescriptorPoolSize> poolSizes{uniformPoolSize, imageSamplerPoolSize, storageBufferPoolSize};

    VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolInfo.pPoolSizes = poolSizes.data();
    descriptorPoolInfo.maxSets = 20;

    VK_CHECK(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    logs("[+] Created Descriptor Pool of count " << descriptorPoolInfo.poolSizeCount);

    // Set Layout
    VkDescriptorSetLayoutBinding uniformLayoutBinding = {};
    uniformLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uniformLayoutBinding.binding = 0;
    uniformLayoutBinding.descriptorCount = 1;

    // material layout binding
    VkDescriptorSetLayoutBinding materialLayoutBinding = {};
    materialLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    materialLayoutBinding.binding = 1;
    materialLayoutBinding.descriptorCount = 1;

    // skeleton animation but not maybe not needed :shrug:
    VkDescriptorSetLayoutBinding meshLayoutBinding = {};
    meshLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    meshLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    meshLayoutBinding.binding = 2;
    meshLayoutBinding.descriptorCount = 1;
    
    // Texture sampler binding
    VkDescriptorSetLayoutBinding textureSamplerBinding = {};
    textureSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    textureSamplerBinding.binding = 3;
    textureSamplerBinding.descriptorCount = 1;

    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {uniformLayoutBinding, materialLayoutBinding, meshLayoutBinding, textureSamplerBinding};
    VkDescriptorSetLayoutCreateInfo descriptorLayout{};
    descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayout.pBindings = setLayoutBindings.data();
    descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
    VK_CHECK(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));
    logs("[+] Created Layout Binding of count " << descriptorLayout.bindingCount);
    
    // Pipeline layout TODO: rewrite this in the modern style found in the newer vulkan examples ... 
    // see examples/imgui/main.cpp :: setupDescriptors() & preparePipelines()
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4) + sizeof(glm::vec4) + sizeof(int); // TODO why the hardcode?? should be sizeof(pushconstblock)

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
    logs("[?] Created Pipeline Layout");

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = descriptorPool;
    descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    VK_CHECK(vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &descriptorSet));
    logs("[+] Allocated Descriptor Set ");

    VkWriteDescriptorSet writeDescriptorSet{};
    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.dstSet = descriptorSet;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writeDescriptorSet.dstBinding = 0;
    writeDescriptorSet.pBufferInfo = &uniformBufferVS.descriptor;
    writeDescriptorSet.descriptorCount = 1;
    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {writeDescriptorSet};
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
    logs("[+] Updated Descriptor Sets ");
}

void Gpu::setupMeshDescriptors()
{
    logs("[+] Setting up mesh descriptors");
    
    for (size_t modelIndex = 0; modelIndex < gltfLoader.getModelCount(); ++modelIndex)
    {
        Model *model = gltfLoader.getModel(modelIndex);
        if (!model) continue;
        
        for (size_t meshIndex = 0; meshIndex < model->meshes.size(); ++meshIndex)
        {
            auto &mesh = model->meshes[meshIndex];
            
            // Allocate descriptor set for this mesh
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &descriptorSetLayout;
            
            VK_CHECK(vkAllocateDescriptorSets(vulkanDevice.logicalDevice, &allocInfo, &mesh.descriptorSet));
            
            // Write descriptor set
            std::vector<VkWriteDescriptorSet> writeDescriptorSets;
            
            // Uniform buffer (same for all meshes)
            VkWriteDescriptorSet uniformWrite{};
            uniformWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            uniformWrite.dstSet = mesh.descriptorSet;
            uniformWrite.dstBinding = 0;
            uniformWrite.dstArrayElement = 0;
            uniformWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uniformWrite.descriptorCount = 1;
            uniformWrite.pBufferInfo = &uniformBufferVS.descriptor;
            writeDescriptorSets.push_back(uniformWrite);
            
            // Material buffer
            VkWriteDescriptorSet materialWrite{};
            materialWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            materialWrite.dstSet = mesh.descriptorSet;
            materialWrite.dstBinding = 1;
            materialWrite.dstArrayElement = 0;
            materialWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            materialWrite.descriptorCount = 1;
            materialWrite.pBufferInfo = &mesh.shaderMaterialBuffer.descriptor;
            writeDescriptorSets.push_back(materialWrite);
            
            // Mesh data buffer
            VkWriteDescriptorSet meshWrite{};
            meshWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            meshWrite.dstSet = mesh.descriptorSet;
            meshWrite.dstBinding = 2;
            meshWrite.dstArrayElement = 0;
            meshWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            meshWrite.descriptorCount = 1;
            meshWrite.pBufferInfo = &mesh.shaderMeshBuffer.descriptor;
            writeDescriptorSets.push_back(meshWrite);
            
            // Texture sampler
            // Find the first texture available in the model, or use a default white texture
            VkDescriptorImageInfo imageInfo{};
            
            if (!model->images.empty() && model->images[0].gpu.view != VK_NULL_HANDLE)
            {
                imageInfo.imageView = model->images[0].gpu.view;
                imageInfo.sampler = model->images[0].gpu.sampler;
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            else
            {
                // Use default white texture
                imageInfo.imageView = defaultWhiteTexture.view;
                imageInfo.sampler = defaultWhiteTexture.sampler;
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            
            VkWriteDescriptorSet textureWrite{};
            textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            textureWrite.dstSet = mesh.descriptorSet;
            textureWrite.dstBinding = 3;
            textureWrite.dstArrayElement = 0;
            textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            textureWrite.descriptorCount = 1;
            textureWrite.pImageInfo = &imageInfo;
            writeDescriptorSets.push_back(textureWrite);
            
            // Update descriptor set
            vkUpdateDescriptorSets(vulkanDevice.logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), 
                                 writeDescriptorSets.data(), 0, nullptr);
            
            bool hasModelTexture = (!model->images.empty() && model->images[0].gpu.view != VK_NULL_HANDLE);
            logs("[+] Created descriptor set for mesh " << meshIndex << " in model " << modelIndex << 
                 (hasModelTexture ? " (with texture)" : " (with default texture)"));
        }
    }
}

void Gpu::prepareRenderingPipelines()
{
}



void Gpu::preparePipelines(VkDevice device)
{
    // TODO I do not like this
    VkPipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo{};
    pipelineInputAssemblyStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    pipelineInputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineInputAssemblyStateCreateInfo.flags = 0;
    pipelineInputAssemblyStateCreateInfo.primitiveRestartEnable = VK_FALSE;

    VkPipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo{};
    pipelineRasterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    pipelineRasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
    pipelineRasterizationStateCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    pipelineRasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipelineRasterizationStateCreateInfo.flags = 0;
    pipelineRasterizationStateCreateInfo.depthClampEnable = VK_FALSE;
    pipelineRasterizationStateCreateInfo.lineWidth = 1.0f;

    VkPipelineColorBlendAttachmentState pipelineColorBlendAttachmentState{};
    pipelineColorBlendAttachmentState.colorWriteMask = 0xf;
    pipelineColorBlendAttachmentState.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo{};
    pipelineColorBlendStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    pipelineColorBlendStateCreateInfo.attachmentCount = 1;
    pipelineColorBlendStateCreateInfo.pAttachments = &pipelineColorBlendAttachmentState;

    VkPipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo{};
    pipelineDepthStencilStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    pipelineDepthStencilStateCreateInfo.depthTestEnable = VK_TRUE;
    pipelineDepthStencilStateCreateInfo.depthWriteEnable = VK_TRUE;
    pipelineDepthStencilStateCreateInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    pipelineDepthStencilStateCreateInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineViewportStateCreateInfo pipelineViewportStateCreateInfo{};
    pipelineViewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    pipelineViewportStateCreateInfo.viewportCount = 1;
    pipelineViewportStateCreateInfo.scissorCount = 1;
    pipelineViewportStateCreateInfo.flags = 0;

    VkPipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo{};
    pipelineMultisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    pipelineMultisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    pipelineMultisampleStateCreateInfo.flags = 0;

    std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo{};
	pipelineDynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	pipelineDynamicStateCreateInfo.pDynamicStates = dynamicStateEnables.data();
	pipelineDynamicStateCreateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());
	pipelineDynamicStateCreateInfo.flags = 0;

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

    /* Notice how this is the point we actually create the pipeline */
    
    // TODO: Consider how we set up the pipeline layout and render pass in earlier code...
    VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.layout = pipelineLayout;
    pipelineCreateInfo.renderPass = renderPass;
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
    pipelineCreateInfo.pVertexInputState = vertex::getPipelineVertexInputState({VertexComponent::Position, VertexComponent::Normal, VertexComponent::UV, VertexComponent::Color});

    shaderStages[0] = loadShader(scene_vert_spv, scene_vert_spv_size, VK_SHADER_STAGE_VERTEX_BIT, device);
    shaderStages[1] = loadShader(scene_frag_spv, scene_frag_spv_size, VK_SHADER_STAGE_FRAGMENT_BIT, device);

    VK_CHECK(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
    logs("[+] Prepared pipelines with stage count " << pipelineCreateInfo.stageCount);
}

VkShaderModule loadShaderFromMemory(const uint32_t *code, size_t size, VkDevice device)
{
    VkShaderModule shaderModule;
    VkShaderModuleCreateInfo moduleCreateInfo{};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = nullptr;
    moduleCreateInfo.flags = 0;
    moduleCreateInfo.codeSize = size;
    moduleCreateInfo.pCode = code;
    VK_CHECK(vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &shaderModule));
    return shaderModule;
}

// TODO: refactor this to retain shader modules in memory, so we don't have to re-load at runtime
VkPipelineShaderStageCreateInfo Gpu::loadShader(const uint32_t *code, size_t size, VkShaderStageFlagBits stage, VkDevice device)
{
    VkPipelineShaderStageCreateInfo shaderStage = {};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = stage;
    shaderStage.module = loadShaderFromMemory(code, size, device);
    shaderStage.pName = "main";
    assert(shaderStage.module != VK_NULL_HANDLE);
    // shaderModules.push_back(shaderStage.module);
    return shaderStage;
};

// Create an image memory barrier for changing the layout of
// an image and put it into an active command buffer
// See chapter 11.4 "Image Layout" for details
void Gpu::setImageLayout(VkCommandBuffer cmdbuffer, VkImage image, VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
                         VkImageSubresourceRange subresourceRange, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask)
{
    // Create an image barrier object
    VkImageMemoryBarrier imageMemoryBarrier{};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.oldLayout = oldImageLayout;
    imageMemoryBarrier.newLayout = newImageLayout;
    imageMemoryBarrier.image = image;
    imageMemoryBarrier.subresourceRange = subresourceRange;

    // Source layouts (old)
    // Source access mask controls actions that have to be finished on the old layout
    // before it will be transitioned to the new layout
    switch (oldImageLayout)
    {
        case VK_IMAGE_LAYOUT_UNDEFINED:
        // Image layout is undefined (or does not matter)
	    // Only valid as initial layout
	    // No flags required, listed only for completeness
	    imageMemoryBarrier.srcAccessMask = 0;
	    break;

	    case VK_IMAGE_LAYOUT_PREINITIALIZED:
        // Image is preinitialized
        // Only valid as initial layout for linear images, preserves memory contents
        // Make sure host writes have been finished
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        // Image is a color attachment
        // Make sure any writes to the color buffer have been finished
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        // Image is a depth/stencil attachment
        // Make sure any writes to the depth/stencil buffer have been finished
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        // Image is a transfer source
        // Make sure any reads from the image have been finished
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;

        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        // Image is a transfer destination
        // Make sure any writes to the image have been finished
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        // Image is read by a shader
        // Make sure any shader reads from the image have been finished
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
        default:
        // Other source layouts aren't handled (yet)
        break;
    }

    // Target layouts (new)
    // Destination access mask controls the dependency for the new image layout
    switch (newImageLayout)
    {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        // Image will be used as a transfer destination
        // Make sure any writes to the image have been finished
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        // Image will be used as a transfer source
        // Make sure any reads from the image have been finished
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;

        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        // Image will be used as a color attachment
        // Make sure any writes to the color buffer have been finished
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        // Image layout will be used as a depth/stencil attachment
        // Make sure any writes to depth/stencil buffer have been finished
        imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        // Image will be read in a shader (sampler, input attachment)
        // Make sure any writes to the image have been finished
        if (imageMemoryBarrier.srcAccessMask == 0)
            imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
        default:
        // Other source layouts aren't handled (yet)
        break;
    }

    // Put barrier inside setup command buffer
    vkCmdPipelineBarrier(cmdbuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
}

bool Gpu::loadGLTFModel(const std::string &filename)
{
    gltfLoader.device = &vulkanDevice;
    int modelIndex = gltfLoader.loadglTFFile(filename);

    if (modelIndex >= 0)
    {
        Model *model = gltfLoader.getModel(modelIndex);
        logs("[+] Successfully loaded GLTF model: " << filename);
        logs("[+] Model contains " << model->meshes.size() << " meshes");
        logs("[+] Model contains " << model->nodes.size() << " nodes");
        return true;
    }
    else
    {
        logs("[!] Failed to load GLTF model: " << filename);
        return false;
    }
}

void Gpu::renderGLTFModel(VkCommandBuffer commandBuffer)
{
    if (gltfLoader.getModelCount() == 0)
    {
        logs("[DEBUG] No GLTF models loaded");
        return; 
    }

    //logs("[DEBUG] Rendering " << gltfLoader.getModelCount() << " GLTF models");
    
    // Bind the pipeline
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    
    // Bind descriptor sets
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    
    // Set up push constants for the model
    struct
    {
        glm::mat4 transform;
        glm::vec4 effectColor;
        int32_t effectType;
    } pushConstants;

    pushConstants.transform = glm::mat4(1.0f);                     // Identity matrix
    pushConstants.effectColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f); // White
    pushConstants.effectType = 0;                                  // No effect

    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(pushConstants),
        &pushConstants);

    // Render all loaded models
    for (size_t modelIndex = 0; modelIndex < gltfLoader.getModelCount(); ++modelIndex)
    {
        Model *model = gltfLoader.getModel(modelIndex);
        if (!model)
        {
            // logs("[DEBUG] Model " << modelIndex << " is null");
            continue;
        }

        // logs("[DEBUG] Model " << modelIndex << " has " << model->meshes.size() << " meshes");
        
        // Render all meshes in this model
        for (size_t meshIndex = 0; meshIndex < model->meshes.size(); ++meshIndex)
        {
            const auto &mesh = model->meshes[meshIndex];
            if (mesh.vertexCount == 0)
            {
                logs("[DEBUG] Mesh " << meshIndex << " has 0 vertices, skipping");
                continue;
            }

            //logs("[DEBUG] Rendering mesh " << meshIndex << " with " << mesh.vertexCount << " vertices, " << mesh.indexCount << " indices");

            if (mesh.descriptorSet != VK_NULL_HANDLE)
            {
                // logs("[DEBUG] Binding descriptor set for mesh " << meshIndex);
                // vkCmdBindDescriptorSets(
                //     commandBuffer,
                //     VK_PIPELINE_BIND_POINT_GRAPHICS,
                //     pipelineLayout,
                //     0, // First set
                //     1, // Bind 1 set
                //     &mesh.descriptorSet,
                //     0, nullptr);
            }
            else
            {
                logs("[DEBUG] Mesh " << meshIndex << " has null descriptor set");
            }
            
            // Bind vertex buffer
            VkBuffer vertexBuffers[] = {mesh.vertexBuffer.buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            
            // Bind index buffer and draw
            if (mesh.indexCount > 0)
            {
                // logs("[DEBUG] Drawing indexed: " << mesh.indexCount << " indices");
                vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
            }
            else
            {
                // logs("[DEBUG] Drawing non-indexed: " << mesh.vertexCount << " vertices");
                vkCmdDraw(commandBuffer, mesh.vertexCount, 1, 0, 0);
            }
        }
    }
}

void Gpu::renderGLTFModel(VkCommandBuffer commandBuffer, glm::mat4 transform, bool wireframe)
{
    if (gltfLoader.getModelCount() == 0)
    {
        return;
    }

    // Bind the pipeline
    // TODO: Implement wireframe pipeline (VK_POLYGON_MODE_LINE) when needed
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    for (size_t modelIndex = 0; modelIndex < gltfLoader.getModelCount(); ++modelIndex)
    {
        Model *model = gltfLoader.getModel(modelIndex);
        if (!model)
            continue;

        for (size_t meshIndex = 0; meshIndex < model->meshes.size(); ++meshIndex)
        {
            auto &mesh = model->meshes[meshIndex];

            // Bind vertex and index buffers
            VkDeviceSize offsets[1] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, offsets);

            if (mesh.indexCount > 0)
            {
                vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            }

            // Bind descriptor set for this mesh
            VkDescriptorSet descriptorSet = mesh.descriptorSet;
            if (descriptorSet == VK_NULL_HANDLE)
            {
                descriptorSet = this->descriptorSet; // Fallback
            }

            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

            // Set up push constants with the provided transform
            struct
            {
                glm::mat4 transform;
                glm::vec4 effectColor;
                int32_t effectType;
            } pushConstants;

            pushConstants.transform = transform;
            pushConstants.effectColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            pushConstants.effectType = 0;

            vkCmdPushConstants(
                commandBuffer,
                pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(pushConstants),
                &pushConstants);

            // Draw
            if (mesh.indexCount > 0)
            {
                vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
            }
            else
            {
                vkCmdDraw(commandBuffer, mesh.vertexCount, 1, 0, 0);
            }
        }
    }
}

void Gpu::createDefaultWhiteTexture()
{
    // Create a 1x1 white texture as default (RGBA format)
    unsigned char whitePixel[4] = {255, 255, 255, 255}; // RGBA = white with full opacity
    VkDeviceSize size = 4;
    
    // Create staging buffer
    VulkanBuffer stagingBuffer;
    VK_CHECK(vulkanDevice.createBuffer(
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingBuffer,
        size,
        whitePixel
    ));
    
    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {1, 1, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    VK_CHECK(vkCreateImage(vulkanDevice.logicalDevice, &imageInfo, nullptr, &defaultWhiteTexture.image));
    
    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(vulkanDevice.logicalDevice, defaultWhiteTexture.image, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = vulkanDevice.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr);
    
    VK_CHECK(vkAllocateMemory(vulkanDevice.logicalDevice, &allocInfo, nullptr, &defaultWhiteTexture.memory));
    VK_CHECK(vkBindImageMemory(vulkanDevice.logicalDevice, defaultWhiteTexture.image, defaultWhiteTexture.memory, 0));
    
    // Copy data
    VkCommandBuffer copyCmd = vulkanDevice.createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, vulkanDevice.transferCommandPool, true);
    
    // Transition to transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = defaultWhiteTexture.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // Copy buffer to image
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {1, 1, 1};
    
    vkCmdCopyBufferToImage(copyCmd, stagingBuffer.buffer, defaultWhiteTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    vulkanDevice.flushCommandBuffer(copyCmd, vulkanDevice.transferQueue, vulkanDevice.transferCommandPool, true);
    
    stagingBuffer.destroy();
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = defaultWhiteTexture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    
    VK_CHECK(vkCreateImageView(vulkanDevice.logicalDevice, &viewInfo, nullptr, &defaultWhiteTexture.view));
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    
    VK_CHECK(vkCreateSampler(vulkanDevice.logicalDevice, &samplerInfo, nullptr, &defaultWhiteTexture.sampler));
    
    defaultWhiteTexture.width = 1;
    defaultWhiteTexture.height = 1;
    defaultWhiteTexture.format = VK_FORMAT_R8G8B8A8_UNORM;
    
    logs("[+] Created default white texture");
}

void Gpu::updateCameraFromInput(Inputs& inputs, float deltaTime)
{
    ImGuiIO& io = ImGui::GetIO();
    
    // Only process camera controls if ImGui is not capturing input
    if (io.WantCaptureMouse || io.WantCaptureKeyboard)
    {
        return;
    }
    
    // Mouse rotation (right mouse button drag)
    if (inputs.mouseState.buttons.right)
    {
        glm::vec2 mouseDelta = inputs.mouseState.position - inputs.mouseState.lastPosition;
        camera.rotation.y += mouseDelta.x * camera.rotationSpeed * 0.5f;
        camera.rotation.x += mouseDelta.y * camera.rotationSpeed * 0.5f;
        
        // Clamp pitch to avoid gimbal lock
        camera.rotation.x = glm::clamp(camera.rotation.x, -89.0f, 89.0f);
        
        camera.updateViewMatrix();
    }
    
    // Keyboard movement (WASD)
    bool wPressed = (GetKeyState('W') & 0x8000) != 0;
    bool sPressed = (GetKeyState('S') & 0x8000) != 0;
    bool aPressed = (GetKeyState('A') & 0x8000) != 0;
    bool dPressed = (GetKeyState('D') & 0x8000) != 0;
    bool qPressed = (GetKeyState('Q') & 0x8000) != 0;
    bool ePressed = (GetKeyState('E') & 0x8000) != 0;
    
    camera.keys.up = wPressed;
    camera.keys.down = sPressed;
    camera.keys.left = aPressed;
    camera.keys.right = dPressed;
    
    // Q/E for up/down movement
    if (qPressed)
    {
        camera.position.y -= camera.movementSpeed * deltaTime;
        camera.updateViewMatrix();
    }
    if (ePressed)
    {
        camera.position.y += camera.movementSpeed * deltaTime;
        camera.updateViewMatrix();
    }
}