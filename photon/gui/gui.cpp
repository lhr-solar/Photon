/*[μ] the photon graphical user interface*/
#include <stdio.h>
#include <string>
#include <cstring>
#include <vulkan/vulkan.h>

#include "vulkan_core.h"
#include "gui.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include "../engine/include.hpp"
#include "../gpu/vulkanDevice.hpp"
#include "../gpu/vulkanBuffer.hpp"
#include "../gpu/gpu.hpp"
#include "ui_frag_spv.hpp"
#include "ui_vert_spv.hpp"

Gui::Gui(){};
Gui::~Gui(){
#ifdef XCB
    xcb_destroy_window(connection, window);
	xcb_disconnect(connection);
#endif
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
    descriptorPoolSize.descriptorCount = 2;
    std::vector<VkDescriptorPoolSize> poolSizes = { descriptorPoolSize };

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolInfo.pPoolSizes = poolSizes.data();
    descriptorPoolInfo.maxSets = 2;
    VK_CHECK(vkCreateDescriptorPool(vulkanDevice.logicalDevice, &descriptorPoolInfo, nullptr, &guiDescriptorPool));
    logs("[+] Created Gui Descriptor Pool");

    // Descriptor set Layout
    VkDescriptorSetLayoutBinding setLayoutBinding0 {};
    setLayoutBinding0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    setLayoutBinding0.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    setLayoutBinding0.binding = 0;
    setLayoutBinding0.descriptorCount = 1;

    VkDescriptorSetLayoutBinding setLayoutBinding1 {};
    setLayoutBinding1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    setLayoutBinding1.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    setLayoutBinding1.binding = 1;
    setLayoutBinding1.descriptorCount = 1;

    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {setLayoutBinding0, setLayoutBinding1};

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

    VkDescriptorImageInfo sceneDescriptorImageInfo {};
    sceneDescriptorImageInfo.sampler = sampler;
    sceneDescriptorImageInfo.imageView = fontView;
    sceneDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo fontDescriptorImageInfo {};
    fontDescriptorImageInfo.sampler = sampler;
    fontDescriptorImageInfo.imageView = fontView;
    fontDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet sceneWriteDescriptorSet {};
    sceneWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sceneWriteDescriptorSet.dstSet = guiDescriptorSet;
    sceneWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sceneWriteDescriptorSet.dstBinding = 0;
    sceneWriteDescriptorSet.pImageInfo = &sceneDescriptorImageInfo;
    sceneWriteDescriptorSet.descriptorCount = 1;

    VkWriteDescriptorSet fontWriteDescriptorSet {};
    fontWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    fontWriteDescriptorSet.dstSet = guiDescriptorSet;
    fontWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    fontWriteDescriptorSet.dstBinding = 0;
    fontWriteDescriptorSet.pImageInfo = &sceneDescriptorImageInfo;
    fontWriteDescriptorSet.descriptorCount = 1;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {sceneWriteDescriptorSet, fontWriteDescriptorSet};

    vkUpdateDescriptorSets(vulkanDevice.logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
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

void Gui::buildCommandBuffers(VulkanDevice vulkanDevice, VkRenderPass renderPass, std::vector<VkFramebuffer> frameBuffers, std::vector<VkCommandBuffer> drawCmdBuffers){
    VkCommandBufferBeginInfo cmdBufferBeginInfo {};
    cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkClearValue clearValues[2];
    clearValues[0].color = {{1.0f, 0.82f, 0.86f, 1.0f}};
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

    ImGui::NewFrame();
    ImPlot::ShowDemoWindow();
    ImGui::Render();

    updateBuffers(vulkanDevice);

    for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
        renderPassBeginInfo.framebuffer = frameBuffers[i];
        VK_CHECK(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufferBeginInfo));
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
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, guiPipelineLayout, 0, 1, &guiDescriptorSet, 0, nullptr);
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
      for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
        const ImDrawList *cmd_list = imDrawData->CmdLists[i];
        for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {
          const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[j];
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
	window = CreateWindowEx(0,
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
		NULL);
    if (!window){
		std::cerr << "Could not create window!\n";
		fflush(stdout);
		return;
	}

    ShowWindow(window, SW_SHOW);
	SetForegroundWindow(window);
	SetFocus(window);
}
#endif