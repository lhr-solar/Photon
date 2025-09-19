/*[μ] the photon graphical user interface*/
#include <stdio.h>
#include <string>
#include <cstring>
#include <vulkan/vulkan.h>

#include "vulkan_core.h"
#include "gui.hpp"
#include "ui.hpp"
#include "inputs.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include "../engine/include.hpp"
#include "../gpu/vulkanDevice.hpp"
#include "../gpu/vulkanBuffer.hpp"
#include "../gpu/gpu.hpp"
#include "ui_frag_spv.hpp"
#include "ui_vert_spv.hpp"
#include "custom_shader_frag_spv.hpp"
#include "custom_shader_vert_spv.hpp"

#ifdef WIN
#include <windowsx.h>
#endif

Gui::Gui(){};
Gui::~Gui(){
#ifdef XCB
    xcb_destroy_window(connection, window);
	xcb_disconnect(connection);
#endif
    ImGui::DestroyContext();
    ImPlot::DestroyContext();
    ImPlot3D::DestroyContext();
};

#ifdef XCB
void Gui::initWindow(){
    initxcbConnection();
    setupWindow();
}
#endif

std::string Gui::getWindowTitle() const {
    // TODO: accelerator + fps
	std::string windowTitle =  title ; //+ deviceProperties.deviceName };
    //windowTitle += " - " + std::to_string(frameCounter) + " fps";
	return windowTitle;
}

void Gui::prepareImGui(){
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImPlot3D::CreateContext();
    logs("[+] Created ImX context!");

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.IniFilename = nullptr;
}

// initialize all vulkan resources used by the UI
void Gui::initResources(VulkanDevice vulkanDevice, VkRenderPass renderPass){
    std::strncpy(ui.deviceName, vulkanDevice.deviceProperties.deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
    ui.deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1] = '\0';
    ui.vendorID = vulkanDevice.deviceProperties.vendorID;
    ui.deviceID = vulkanDevice.deviceProperties.deviceID;
    ui.deviceType = vulkanDevice.deviceProperties.deviceType;
    ui.driverVersion = vulkanDevice.deviceProperties.driverVersion;
    ui.apiVersion = vulkanDevice.deviceProperties.apiVersion;

    ImGuiIO &io = ImGui::GetIO();
    unsigned char *fontData;
    int texWidth, texHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    VkDeviceSize uploadSize = texWidth * texHeight * 4 * sizeof(char);
    VkImageCreateInfo imageCreateInfo {};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageCreateInfo.extent.width = texWidth;
    imageCreateInfo.extent.height = texHeight;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(vulkanDevice.logicalDevice, &imageCreateInfo, nullptr, &fontImage);
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(vulkanDevice.logicalDevice, fontImage, &memReqs);
    VkMemoryAllocateInfo memAllocInfo {};
    memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = vulkanDevice.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr);
    VK_CHECK(vkAllocateMemory(vulkanDevice.logicalDevice, &memAllocInfo, nullptr, &fontMemory));
    logs("[+] Allocated font memory");
    VK_CHECK(vkBindImageMemory(vulkanDevice.logicalDevice, fontImage, fontMemory, 0));
    logs("[+] Bound font memory");

    VkImageViewCreateInfo imageViewCreateInfo {};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image = fontImage;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(vulkanDevice.logicalDevice, &imageViewCreateInfo, nullptr, &fontView));
    logs("[+] Created Font Image View");

    VulkanBuffer stagingBuffer;
    VK_CHECK(vulkanDevice.createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, uploadSize, nullptr));
    logs("[+] Created staging buffer");
    stagingBuffer.map(VK_WHOLE_SIZE, 0);
    memcpy(stagingBuffer.mapped, fontData, uploadSize);
    stagingBuffer.unmap();

    VkCommandBuffer copyCmd = vulkanDevice.createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, vulkanDevice.graphicsCommandPool, true);
    logs("[+] Created Command Buffer from Graphics Command Pool");
    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.layerCount = 1;
    Gpu::setImageLayout(copyCmd, fontImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    logs("[+] Set Image Layout for Transfer");

    VkBufferImageCopy bufferCopyRegion = {};
    bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bufferCopyRegion.imageSubresource.layerCount = 1;
    bufferCopyRegion.imageExtent.width = texWidth;
    bufferCopyRegion.imageExtent.height = texHeight;
    bufferCopyRegion.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(copyCmd, stagingBuffer.buffer, fontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);
    logs("[+] Copied Buffer to Image");

    Gpu::setImageLayout(copyCmd, fontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vulkanDevice.flushCommandBuffer(copyCmd, vulkanDevice.graphicsQueue, vulkanDevice.graphicsCommandPool, true);
    logs("[+] Flushed Command Buffer");
    stagingBuffer.destroy();
    logs("[+] Destroyed Staging Buffer");

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
    vkCreateSampler(vulkanDevice.logicalDevice, &samplerCreateInfo, nullptr, &sampler);
    logs("[+] Created Gui Sampler");

    // Descriptor Pool
    VkDescriptorPoolSize descriptorPoolSize {};
    descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorPoolSize.descriptorCount = 8;
    std::vector<VkDescriptorPoolSize> poolSizes = { descriptorPoolSize };

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolInfo.pPoolSizes = poolSizes.data();
    descriptorPoolInfo.maxSets = 8;
    VK_CHECK(vkCreateDescriptorPool(vulkanDevice.logicalDevice, &descriptorPoolInfo, nullptr, &guiDescriptorPool));
    logs("[+] Created Gui Descriptor Pool");

    // Descriptor set Layout
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
    VK_CHECK(vkCreateDescriptorSetLayout(vulkanDevice.logicalDevice, &descriptorSetLayoutCreateInfo, nullptr, &guiDescriptorSetLayout));
    logs("[+] Created Gui Descriptor Set Layout");

    // Descriptor Set
    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo {};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = guiDescriptorPool;
    descriptorSetAllocateInfo.pSetLayouts = &guiDescriptorSetLayout;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    VK_CHECK(vkAllocateDescriptorSets(vulkanDevice.logicalDevice, &descriptorSetAllocateInfo, &guiDescriptorSet));
    logs("[+] Allocated Gui Descriptor Set ");

    VkDescriptorImageInfo fontDescriptorImageInfo {};
    fontDescriptorImageInfo.sampler = sampler;
    fontDescriptorImageInfo.imageView = fontView;
    fontDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet fontWriteDescriptorSet {};
    fontWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    fontWriteDescriptorSet.dstSet = guiDescriptorSet;
    fontWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    fontWriteDescriptorSet.dstBinding = 0;
    fontWriteDescriptorSet.pImageInfo = &fontDescriptorImageInfo;
    fontWriteDescriptorSet.descriptorCount = 1;

    vkUpdateDescriptorSets(vulkanDevice.logicalDevice, 1, &fontWriteDescriptorSet, 0, nullptr);
    io.Fonts->TexID = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(guiDescriptorSet));
    ui.customShaderTexture = static_cast<ImTextureID>(0);
    ui.customShaderTextureSize = ImVec2(0.0f, 0.0f);

    initCustomShaderResources(vulkanDevice);
    logs("[+] Updated Gui Descriptor Sets ");

    // Pipeline cache
    VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
    pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    VK_CHECK(vkCreatePipelineCache(vulkanDevice.logicalDevice, &pipelineCacheCreateInfo, nullptr, &guiPipelineCache));
    logs("[+] Create Gui Pipeline Cache");

    // Pipeline layout & Push constants for UI rendering
    VkPushConstantRange pushConstantRange {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(pushConstBlock);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo {};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &guiDescriptorSetLayout;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
    VK_CHECK(vkCreatePipelineLayout(vulkanDevice.logicalDevice, &pipelineLayoutCreateInfo, nullptr, &guiPipelineLayout));
    logs("[+] Created Gui Pipeline Layout ");

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

    // Enable blending
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

    VkPipelineViewportStateCreateInfo pipelineViewportStateCreateInfo {};
    pipelineViewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	pipelineViewportStateCreateInfo.viewportCount = 1;
	pipelineViewportStateCreateInfo.scissorCount = 1;
	pipelineViewportStateCreateInfo.flags = 0;

    VkPipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo {};
    pipelineMultisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    pipelineMultisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    pipelineMultisampleStateCreateInfo.flags = 0;

    std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo{};
    pipelineDynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    pipelineDynamicStateCreateInfo.pDynamicStates = dynamicStateEnables.data();
    pipelineDynamicStateCreateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());
    pipelineDynamicStateCreateInfo.flags = 0;

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

    VkGraphicsPipelineCreateInfo pipelineCreateInfo {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.layout = guiPipelineLayout;
    pipelineCreateInfo.renderPass = renderPass;
    pipelineCreateInfo.flags = 0;
    pipelineCreateInfo.basePipelineIndex = -1;
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE; 

    /*...[]__
             --[    ]*/
    pipelineCreateInfo.pInputAssemblyState = &pipelineInputAssemblyStateCreateInfo;
    pipelineCreateInfo.pRasterizationState = &pipelineRasterizationStateCreateInfo;
    pipelineCreateInfo.pColorBlendState = &pipelineColorBlendStateCreateInfo;
    pipelineCreateInfo.pMultisampleState = &pipelineMultisampleStateCreateInfo;
    pipelineCreateInfo.pViewportState = &pipelineViewportStateCreateInfo;
    pipelineCreateInfo.pDepthStencilState = &pipelineDepthStencilStateCreateInfo;
    pipelineCreateInfo.pDynamicState = &pipelineDynamicStateCreateInfo;
    pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCreateInfo.pStages = shaderStages.data();

    // Vertex bindings an attributes based on ImGui vertex definition
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

    /*... []*/
    pipelineCreateInfo.pVertexInputState = &pipelineVertexInputStateCreateInfo;

    shaderStages[0] = Gpu::loadShader(ui_vert_spv, ui_vert_spv_size, VK_SHADER_STAGE_VERTEX_BIT, vulkanDevice.logicalDevice);
    shaderStages[1] = Gpu::loadShader(ui_frag_spv, ui_frag_spv_size, VK_SHADER_STAGE_FRAGMENT_BIT, vulkanDevice.logicalDevice);

    vkCreateGraphicsPipelines(vulkanDevice.logicalDevice, guiPipelineCache, 1, &pipelineCreateInfo, nullptr, &guiPipeline);
    logs("[+] Created Graphics Gui Pipeline ");
}

void Gui::initCustomShaderResources(VulkanDevice vulkanDevice){
    customShader.extent = {512, 512};

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = customShader.extent.width;
    imageInfo.extent.height = customShader.extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(vulkanDevice.logicalDevice, &imageInfo, nullptr, &customShader.image));

    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(vulkanDevice.logicalDevice, customShader.image, &memReqs);
    VkMemoryAllocateInfo imageAllocInfo{};
    imageAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageAllocInfo.allocationSize = memReqs.size;
    imageAllocInfo.memoryTypeIndex = vulkanDevice.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr);
    VK_CHECK(vkAllocateMemory(vulkanDevice.logicalDevice, &imageAllocInfo, nullptr, &customShader.memory));
    VK_CHECK(vkBindImageMemory(vulkanDevice.logicalDevice, customShader.image, customShader.memory, 0));
    customShader.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    customShader.initialized = false;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = customShader.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(vulkanDevice.logicalDevice, &viewInfo, nullptr, &customShader.view));

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    VK_CHECK(vkCreateRenderPass(vulkanDevice.logicalDevice, &renderPassInfo, nullptr, &customShader.renderPass));

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = customShader.renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &customShader.view;
    framebufferInfo.width = customShader.extent.width;
    framebufferInfo.height = customShader.extent.height;
    framebufferInfo.layers = 1;
    VK_CHECK(vkCreateFramebuffer(vulkanDevice.logicalDevice, &framebufferInfo, nullptr, &customShader.framebuffer));

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(float);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pcRange;
    VK_CHECK(vkCreatePipelineLayout(vulkanDevice.logicalDevice, &pipelineLayoutInfo, nullptr, &customShader.pipelineLayout));

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    std::vector<VkDynamicState> dynamics = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamics.size());
    dynamic.pDynamicStates = dynamics.data();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0] = Gpu::loadShader(custom_shader_vert_spv, custom_shader_vert_spv_size, VK_SHADER_STAGE_VERTEX_BIT, vulkanDevice.logicalDevice);
    shaderStages[1] = Gpu::loadShader(custom_shader_frag_spv, custom_shader_frag_spv_size, VK_SHADER_STAGE_FRAGMENT_BIT, vulkanDevice.logicalDevice);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = customShader.pipelineLayout;
    pipelineInfo.renderPass = customShader.renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.pVertexInputState = &vertexInput;

    VK_CHECK(vkCreateGraphicsPipelines(vulkanDevice.logicalDevice, guiPipelineCache, 1, &pipelineInfo, nullptr, &customShader.pipeline));

    VkDescriptorSetAllocateInfo descriptorAlloc{};
    descriptorAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorAlloc.descriptorPool = guiDescriptorPool;
    descriptorAlloc.descriptorSetCount = 1;
    descriptorAlloc.pSetLayouts = &guiDescriptorSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(vulkanDevice.logicalDevice, &descriptorAlloc, &customShader.descriptorSet));

    VkDescriptorImageInfo imageDescriptor{};
    imageDescriptor.sampler = sampler;
    imageDescriptor.imageView = customShader.view;
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = customShader.descriptorSet;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.pImageInfo = &imageDescriptor;
    vkUpdateDescriptorSets(vulkanDevice.logicalDevice, 1, &write, 0, nullptr);

    ui.customShaderTexture = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(customShader.descriptorSet));
    ui.customShaderTextureSize = ImVec2(static_cast<float>(customShader.extent.width), static_cast<float>(customShader.extent.height));
}

void Gui::recordCustomShaderPass(VkCommandBuffer commandBuffer){
    if (customShader.pipeline == VK_NULL_HANDLE) {
        return;
    }

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    VkImageLayout oldLayout = customShader.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags srcStage = customShader.initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    Gpu::setImageLayout(commandBuffer, customShader.image, oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, range, srcStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkClearValue clearValue{};
    clearValue.color = {{0.05f, 0.04f, 0.10f, 1.0f}};

    VkRenderPassBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = customShader.renderPass;
    beginInfo.framebuffer = customShader.framebuffer;
    beginInfo.renderArea.offset = {0, 0};
    beginInfo.renderArea.extent = customShader.extent;
    beginInfo.clearValueCount = 1;
    beginInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{};
    viewport.width = static_cast<float>(customShader.extent.width);
    viewport.height = static_cast<float>(customShader.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = customShader.extent;
    scissor.offset = {0, 0};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, customShader.pipeline);
    float time = static_cast<float>(ImGui::GetTime());
    vkCmdPushConstants(commandBuffer, customShader.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &time);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    Gpu::setImageLayout(commandBuffer, customShader.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    customShader.initialized = true;
    customShader.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void Gui::buildCommandBuffers(VulkanDevice vulkanDevice, VkRenderPass renderPass, std::vector<VkFramebuffer> frameBuffers, std::vector<VkCommandBuffer> drawCmdBuffers){
    VkCommandBufferBeginInfo cmdBufferBeginInfo {};
    cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkClearValue clearValues[2];
    clearValues[0].color = {{0.0f, 0.00f, 0.00f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassBeginInfo {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width = width;
    renderPassBeginInfo.renderArea.extent.height = height;
    renderPassBeginInfo.clearValueCount = 2;
    renderPassBeginInfo.pClearValues = clearValues;


    ui.build();

    updateBuffers(vulkanDevice);

    for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
        renderPassBeginInfo.framebuffer = frameBuffers[i];
        VK_CHECK(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufferBeginInfo));
        recordCustomShaderPass(drawCmdBuffers[i]);
        vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport {};
        viewport.width = width;
        viewport.height = height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

        VkRect2D rect2D {};
	    rect2D.extent.width = width;
	    rect2D.extent.height = height;
	    rect2D.offset.x = 0;
	    rect2D.offset.y = 0;
        vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &rect2D);

        vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, guiPipelineLayout, 0, 1, &guiDescriptorSet, 0, nullptr);

        VkDeviceSize offsets[1] = {0};

        // TODO: would likely do 3D rendering here? consider the Sascha Vulkan 3D models

        // TODO: this looks big tbh
        drawFrame(drawCmdBuffers[i]);
        vkCmdEndRenderPass(drawCmdBuffers[i]);
        VK_CHECK(vkEndCommandBuffer(drawCmdBuffers[i]));
    };
}

void Gui::updateBuffers(VulkanDevice vulkanDevice){
    ImDrawData *imDrawData = ImGui::GetDrawData();

    // Note: Alignment is done inside buffer creation
    VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
    VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

    if ((vertexBufferSize == 0) || (indexBufferSize == 0)) { 
        logs("[!] updateBuffers: No draw data available (vertexBufferSize=" << vertexBufferSize << ", indexBufferSize=" << indexBufferSize << ")");
        return; 
    }

    // Vertex Buffer
    if ((vertexBuffer.buffer == VK_NULL_HANDLE) || (vertexCount != imDrawData->TotalVtxCount)) {
      vertexBuffer.unmap();
      vertexBuffer.destroy();
      VK_CHECK(vulkanDevice.createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &vertexBuffer, vertexBufferSize, nullptr));
      vertexCount = imDrawData->TotalVtxCount;
      vertexBuffer.map(VK_WHOLE_SIZE, 0);
    }

    // Index buffer
    if ((indexBuffer.buffer == VK_NULL_HANDLE) || (indexCount < imDrawData->TotalIdxCount)) {
      indexBuffer.unmap();
      indexBuffer.destroy();
      VK_CHECK(vulkanDevice.createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &indexBuffer, indexBufferSize, 0));
      indexCount = imDrawData->TotalIdxCount;
      indexBuffer.map(VK_WHOLE_SIZE, 0);
    }

    // Upload data
    ImDrawVert *vtxDst = (ImDrawVert *)vertexBuffer.mapped;
    ImDrawIdx *idxDst = (ImDrawIdx *)indexBuffer.mapped;

    // TODO profile this, consider SIMD
    for (int n = 0; n < imDrawData->CmdListsCount; n++) {
      const ImDrawList *cmd_list = imDrawData->CmdLists[n];
      memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
      memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
      vtxDst += cmd_list->VtxBuffer.Size;
      idxDst += cmd_list->IdxBuffer.Size;
    }
    vertexBuffer.flush(VK_WHOLE_SIZE, 0);
    indexBuffer.flush(VK_WHOLE_SIZE, 0);

}

void Gui::drawFrame(VkCommandBuffer commandBuffer){
    ImGuiIO &io = ImGui::GetIO();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, guiPipeline);
    // TODO: isn't this duplicated setup?
    VkViewport viewport {};
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = 0.0;
    viewport.maxDepth = 1.0;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    pushConstBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
    pushConstBlock.translate = glm::vec2(-1.0f);
    pushConstBlock.invScreenSize = glm::vec2(1.0f / io.DisplaySize.x, 1.0f / io.DisplaySize.y);
    pushConstBlock.whitePixel = glm::vec2(io.Fonts->TexUvWhitePixel.x, io.Fonts->TexUvWhitePixel.y);
    pushConstBlock.gradTop = glm::vec4(1.00f, 1.00f, 1.00f, 1.00f);
    pushConstBlock.gradBottom = glm::vec4(1.00f, 1.00f, 1.00f, 1.00f);
    pushConstBlock.u_time = (float)ImGui::GetTime();
    // TODO cross reference pipline layout(?), i don't think this is all getting pushed
    vkCmdPushConstants(commandBuffer, guiPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstBlock), &pushConstBlock);

    // Render commands
    ImDrawData *imDrawData = ImGui::GetDrawData();
    int32_t vertexOffset = 0;
    int32_t indexOffset = 0;

    // TODO look at this runtime holy f*ck
    if (imDrawData->CmdListsCount > 0) {
      VkDeviceSize offsets[1] = {0};
      vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer.buffer, offsets);
      vkCmdBindIndexBuffer(commandBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);
      VkDescriptorSet boundSet = VK_NULL_HANDLE;
      for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
        const ImDrawList *cmd_list = imDrawData->CmdLists[i];
        for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {
          const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[j];
          VkDescriptorSet textureSet = guiDescriptorSet;
          if (pcmd->TextureId != 0) {
              textureSet = reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(pcmd->TextureId));
          }
          if (textureSet == VK_NULL_HANDLE) {
              textureSet = guiDescriptorSet;
          }
          if (textureSet != boundSet) {
              vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, guiPipelineLayout, 0, 1, &textureSet, 0, nullptr);
              boundSet = textureSet;
          }
          VkRect2D scissorRect;
          scissorRect.offset.x = std::max((int32_t)(pcmd->ClipRect.x), 0);
          scissorRect.offset.y = std::max((int32_t)(pcmd->ClipRect.y), 0);
          scissorRect.extent.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
          scissorRect.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y);
          vkCmdSetScissor(commandBuffer, 0, 1, &scissorRect);
          vkCmdDrawIndexed(commandBuffer, pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
          indexOffset += pcmd->ElemCount;
        }
        vertexOffset += cmd_list->VtxBuffer.Size;
      }
    }
}

#ifdef XCB
xcb_visualtype_t* get_argb_visual(xcb_screen_t* screen) {
    xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen);
    for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
        if (depth_iter.data->depth == 32) {
            xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
            for (; visual_iter.rem; xcb_visualtype_next(&visual_iter)) {
                return visual_iter.data; // Found ARGB visual
            }
        }
    }
    return NULL; // No ARGB visual on this screen
}

void Gui::initxcbConnection(){
    const xcb_setup_t *setup;
    xcb_screen_iterator_t iter;
    int src;

    connection = xcb_connect(NULL, &src);
    assert(connection);
    if(xcb_connection_has_error(connection)){
        printf("[!] Could not find a compatible Vulkan ICD!\n");
        fflush(stdout);
        exit(1);
    }

    setup = xcb_get_setup(connection);
    iter = xcb_setup_roots_iterator(setup);
    while(src-- > 0)
        xcb_screen_next(&iter);
    screen = iter.data;
}

static inline xcb_intern_atom_reply_t* intern_atom_helper(xcb_connection_t *conn, bool only_if_exists, const char *str)
{
	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, only_if_exists, strlen(str), str);
	return xcb_intern_atom_reply(conn, cookie, NULL);
}

void Gui::setupWindow(){
    uint32_t value_mask, value_list[32];

    window = xcb_generate_id(connection);

    xcb_visualtype_t* argb_visual = get_argb_visual(screen);
    xcb_colormap_t colormap = xcb_generate_id(connection);
    xcb_create_colormap(connection, XCB_COLORMAP_ALLOC_NONE, colormap, screen->root, argb_visual->visual_id);


	value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    value_list[0] = screen-> black_pixel;
	value_list[1] =
		XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_KEY_PRESS |
		XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY |
		XCB_EVENT_MASK_POINTER_MOTION |
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE;

    if(settings.fullscreen){
        width = screen->width_in_pixels;
		height = screen->height_in_pixels;
    }
    
    xcb_create_window(connection,
		XCB_COPY_FROM_PARENT,
		window, screen->root,
		0, 0, width, height, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual,
		value_mask, value_list);
    
    xcb_intern_atom_reply_t* reply = intern_atom_helper(connection, true, "WM_PROTOCOLS");
	atom_wm_delete_window = intern_atom_helper(connection, false, "WM_DELETE_WINDOW");

    xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
		window, (*reply).atom, 4, 32, 1,
		&(*atom_wm_delete_window).atom);

    std::string windowTitle = getWindowTitle();
	xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
		window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
		title.size(), windowTitle.c_str());

    free(reply);

    std::string wm_class;
	wm_class = wm_class.insert(0, name);
	wm_class = wm_class.insert(name.size(), 1, '\0');
	wm_class = wm_class.insert(name.size() + 1, title);
	wm_class = wm_class.insert(wm_class.size(), 1, '\0');
	xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, wm_class.size() + 2, wm_class.c_str());

    if (settings.fullscreen){
        xcb_intern_atom_reply_t *atom_wm_state = intern_atom_helper(connection, false, "_NET_WM_STATE");
        xcb_intern_atom_reply_t *atom_wm_fullscreen = intern_atom_helper(connection, false, "_NET_WM_STATE_FULLSCREEN");
        xcb_change_property(connection,
            XCB_PROP_MODE_REPLACE,
            window, atom_wm_state->atom,
            XCB_ATOM_ATOM, 32, 1,
            &(atom_wm_fullscreen->atom));
        free(atom_wm_fullscreen);
        free(atom_wm_state);
    }

	xcb_map_window(connection, window);

    xcb_flush(connection);
}

void Gui::handleEvent(const xcb_generic_event_t *event){
	switch (event->response_type & 0x7f){
	case XCB_CLIENT_MESSAGE:
		if ((*(xcb_client_message_event_t*)event).data.data32[0] ==
			(*atom_wm_delete_window).atom) {
			quit = true;
		}
		break;
	case XCB_MOTION_NOTIFY:{
		xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)event;
        inputs.handleMouseMove((int32_t)motion->event_x, (int32_t)motion->event_y);
		break;
	}
	break;
    case XCB_BUTTON_PRESS:{
        xcb_button_press_event_t *press = (xcb_button_press_event_t *)event;
        if (press->detail == XCB_BUTTON_INDEX_1){
            ImGui::GetIO().AddMouseButtonEvent(0, true);
            inputs.mouseState.buttons.left = true;
        }
        if (press->detail == XCB_BUTTON_INDEX_2){
            ImGui::GetIO().AddMouseButtonEvent(2, true);
            inputs.mouseState.buttons.middle = true;
        }
        if (press->detail == XCB_BUTTON_INDEX_3){
            ImGui::GetIO().AddMouseButtonEvent(1, true);
            inputs.mouseState.buttons.right = true;
        }
    }
	break;
    case XCB_BUTTON_RELEASE:{
        xcb_button_press_event_t *press = (xcb_button_press_event_t *)event;
        if (press->detail == XCB_BUTTON_INDEX_1){
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            inputs.mouseState.buttons.left = false;
        }
        if (press->detail == XCB_BUTTON_INDEX_2){
            ImGui::GetIO().AddMouseButtonEvent(2, false);
            inputs.mouseState.buttons.middle = false;
        }
        if (press->detail == XCB_BUTTON_INDEX_3){
            ImGui::GetIO().AddMouseButtonEvent(1, false);
            inputs.mouseState.buttons.right = false;
        }
    }
    break;
	case XCB_KEY_PRESS:{
        const xcb_key_release_event_t *keyEvent = (const xcb_key_release_event_t *)event;
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard) {
        ImGuiKey key = inputs.translateKey(keyEvent->detail);
        if (key != ImGuiKey_None) { io.AddKeyEvent(key, true); }
        uint8_t kc = keyEvent->detail;
        bool shift = keyEvent->state & XCB_MOD_MASK_SHIFT;
        char c = 0;
        switch (kc) {
        case KEY_A: c = shift ? 'A' : 'a'; break;
        case KEY_B: c = shift ? 'B' : 'b'; break;
        case KEY_C: c = shift ? 'C' : 'c'; break;
        case KEY_D: c = shift ? 'D' : 'd'; break;
        case KEY_E: c = shift ? 'E' : 'e'; break;
        case KEY_F: c = shift ? 'F' : 'f'; break;
        case KEY_G: c = shift ? 'G' : 'g'; break;
        case KEY_H: c = shift ? 'H' : 'h'; break;
        case KEY_I: c = shift ? 'I' : 'i'; break;
        case KEY_J: c = shift ? 'J' : 'j'; break;
        case KEY_K: c = shift ? 'K' : 'k'; break;
        case KEY_L: c = shift ? 'L' : 'l'; break;
        case KEY_M: c = shift ? 'M' : 'm'; break;
        case KEY_N: c = shift ? 'N' : 'n'; break;
        case KEY_O: c = shift ? 'O' : 'o'; break;
        case KEY_P: c = shift ? 'P' : 'p'; break;
        case KEY_Q: c = shift ? 'Q' : 'q'; break;
        case KEY_R: c = shift ? 'R' : 'r'; break;
        case KEY_S: c = shift ? 'S' : 's'; break;
        case KEY_T: c = shift ? 'T' : 't'; break;
        case KEY_U: c = shift ? 'U' : 'u'; break;
        case KEY_V: c = shift ? 'V' : 'v'; break;
        case KEY_W: c = shift ? 'W' : 'w'; break;
        case KEY_X: c = shift ? 'X' : 'x'; break;
        case KEY_Y: c = shift ? 'Y' : 'y'; break;
        case KEY_Z: c = shift ? 'Z' : 'z'; break;

        // — Digits (with shifted symbols) —
        case KEY_1: c = shift ? '!' : '1'; break;
        case KEY_2: c = shift ? '@' : '2'; break;
        case KEY_3: c = shift ? '#' : '3'; break;
        case KEY_4: c = shift ? '$' : '4'; break;
        case KEY_5: c = shift ? '%' : '5'; break;
        case KEY_6: c = shift ? '^' : '6'; break;
        case KEY_7: c = shift ? '&' : '7'; break;
        case KEY_8: c = shift ? '*' : '8'; break;
        case KEY_9: c = shift ? '(' : '9'; break;
        case KEY_0: c = shift ? ')' : '0'; break;

        case KEY_SLASH:       c = shift ? '?' : '/'; break;
        case KEY_PERIOD:      c = shift ? '>' : '.'; break;

        // — Whitespace & controls —
        case KEY_SPACE:       c = ' ';  break;
        case KEY_ENTER:       c = '\n'; break;
        case KEY_TAB:         c = '\t'; break;
        }
        if (c) { io.AddInputCharacter(c); }
        }
	}
	break;
	case XCB_KEY_RELEASE:{
		const xcb_key_release_event_t *keyEvent = (const xcb_key_release_event_t *)event;
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard) {
            ImGuiKey key = inputs.translateKey(keyEvent->detail);
            if (key != ImGuiKey_None) { io.AddKeyEvent(key, false); }
        }
	}
	break;
	case XCB_DESTROY_NOTIFY:
		quit = true;
		break;
	default:
		break;
	}
}
#endif

#ifdef WIN
void Gui::initWindow(HINSTANCE hInstance, WNDPROC wndproc){
    windowInstance = hInstance;
    WNDCLASSEX wndClass{};

	wndClass.cbSize = sizeof(WNDCLASSEX);
	wndClass.style = CS_HREDRAW | CS_VREDRAW;
	wndClass.lpfnWndProc = wndproc;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hInstance = windowInstance;
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wndClass.lpszMenuName = NULL;
	wndClass.lpszClassName = name.c_str();

    if (!RegisterClassEx(&wndClass)){
		std::cout << "Could not register window class!\n";
		fflush(stdout);
		exit(1);
	}

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    DWORD dwExStyle;
	DWORD dwStyle;
    dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
    dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    RECT windowRect = {0L, 0L, (long)width, (long)height };
    AdjustWindowRectEx(&windowRect, dwStyle, FALSE, dwExStyle);
    window = CreateWindowEx(
        dwExStyle,
        name.c_str(),
        title.c_str(),
        dwStyle | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0,
        0,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        NULL,
        NULL,
        hInstance,
        this);
    if (!window){
        std::cerr << "Could not create window!\n";
        fflush(stdout);
        return;
    }

    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    SetFocus(window);
}

LRESULT Gui::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){
    ImGuiIO& io = ImGui::GetIO();

    auto updateModifiers = [&io]() {
        const bool leftCtrl   = (GetKeyState(VK_LCONTROL) & 0x8000) != 0;
        const bool rightCtrl  = (GetKeyState(VK_RCONTROL) & 0x8000) != 0;
        const bool leftShift  = (GetKeyState(VK_LSHIFT) & 0x8000) != 0;
        const bool rightShift = (GetKeyState(VK_RSHIFT) & 0x8000) != 0;
        const bool leftAlt    = (GetKeyState(VK_LMENU) & 0x8000) != 0;
        const bool rightAlt   = (GetKeyState(VK_RMENU) & 0x8000) != 0;
        const bool leftSuper  = (GetKeyState(VK_LWIN) & 0x8000) != 0;
        const bool rightSuper = (GetKeyState(VK_RWIN) & 0x8000) != 0;

        io.AddKeyEvent(ImGuiKey_LeftCtrl, leftCtrl);
        io.AddKeyEvent(ImGuiKey_RightCtrl, rightCtrl);
        io.AddKeyEvent(ImGuiKey_LeftShift, leftShift);
        io.AddKeyEvent(ImGuiKey_RightShift, rightShift);
        io.AddKeyEvent(ImGuiKey_LeftAlt, leftAlt);
        io.AddKeyEvent(ImGuiKey_RightAlt, rightAlt);
        io.AddKeyEvent(ImGuiKey_LeftSuper, leftSuper);
        io.AddKeyEvent(ImGuiKey_RightSuper, rightSuper);

        io.AddKeyEvent(ImGuiMod_Ctrl, leftCtrl || rightCtrl);
        io.AddKeyEvent(ImGuiMod_Shift, leftShift || rightShift);
        io.AddKeyEvent(ImGuiMod_Alt, leftAlt || rightAlt);
        io.AddKeyEvent(ImGuiMod_Super, leftSuper || rightSuper);
    };

    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        quit = true;
        PostQuitMessage(0);
        return 0;
    case WM_SETFOCUS:
        io.AddFocusEvent(true);
        updateModifiers();
        return 0;
    case WM_KILLFOCUS:
        io.AddFocusEvent(false);
        io.AddMouseButtonEvent(0, false);
        io.AddMouseButtonEvent(1, false);
        io.AddMouseButtonEvent(2, false);
        inputs.mouseState.buttons.left = false;
        inputs.mouseState.buttons.right = false;
        inputs.mouseState.buttons.middle = false;
        ReleaseCapture();
        updateModifiers();
        return 0;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            destWidth = LOWORD(lParam);
            destHeight = HIWORD(lParam);
            resized = true;
        }
        return 0;
    case WM_MOUSEMOVE:
        inputs.handleMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONDOWN:
        inputs.mouseState.buttons.left = true;
        io.AddMouseButtonEvent(0, true);
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        inputs.mouseState.buttons.left = false;
        io.AddMouseButtonEvent(0, false);
        if (!(wParam & (MK_RBUTTON | MK_MBUTTON))) {
            ReleaseCapture();
        }
        return 0;
    case WM_RBUTTONDOWN:
        inputs.mouseState.buttons.right = true;
        io.AddMouseButtonEvent(1, true);
        SetCapture(hwnd);
        return 0;
    case WM_RBUTTONUP:
        inputs.mouseState.buttons.right = false;
        io.AddMouseButtonEvent(1, false);
        if (!(wParam & (MK_LBUTTON | MK_MBUTTON))) {
            ReleaseCapture();
        }
        return 0;
    case WM_MBUTTONDOWN:
        inputs.mouseState.buttons.middle = true;
        io.AddMouseButtonEvent(2, true);
        SetCapture(hwnd);
        return 0;
    case WM_MBUTTONUP:
        inputs.mouseState.buttons.middle = false;
        io.AddMouseButtonEvent(2, false);
        if (!(wParam & (MK_LBUTTON | MK_RBUTTON))) {
            ReleaseCapture();
        }
        return 0;
    case WM_MOUSEWHEEL:
        io.AddMouseWheelEvent(0.0f, (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA);
        return 0;
    case WM_MOUSEHWHEEL:
        io.AddMouseWheelEvent((float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA, 0.0f);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        ImGuiKey imguiKey = inputs.translateKey((uint32_t)wParam);
        if (imguiKey != ImGuiKey_None) {
            io.AddKeyEvent(imguiKey, true);
        }
        updateModifiers();
        return 0;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        ImGuiKey imguiKey = inputs.translateKey((uint32_t)wParam);
        if (imguiKey != ImGuiKey_None) {
            io.AddKeyEvent(imguiKey, false);
        }
        updateModifiers();
        return 0;
    }
    case WM_CHAR:
    case WM_SYSCHAR:
        if (wParam > 0 && wParam < 0x10000) {
            io.AddInputCharacter((unsigned int)wParam);
        }
        return 0;
    default:
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
#endif
