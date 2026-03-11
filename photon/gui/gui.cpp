/*[μ] the photon graphical user interface*/
#include <stdio.h>
#include <string>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vulkan/vulkan.h>
#include "vulkan_core.h"
#include "gui.hpp"
#include "ui.hpp"
#include "inputs.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include "imnodes.h"
#include "../engine/include.hpp"
#include "../gpu/vulkanDevice.hpp"
#include "../gpu/vulkanBuffer.hpp"
#include "../gpu/gpu.hpp"
#include "../gpu/vulkanShader.hpp"
#include "ui_frag_spv.hpp"
#include "ui_vert_spv.hpp"
#include "custom_shader_frag_spv.hpp"
#include "custom_shader_vert_spv.hpp"
#include "background_frag_spv.hpp"
#include "background_vert_spv.hpp"
#include "triangle_frag_spv.hpp"
#include "triangle_vert_spv.hpp"
#include "viking_frag_spv.hpp"
#include "viking_vert_spv.hpp"
#include "sansFlex_ttf.hpp"


Gui::Gui(){};
Gui::~Gui(){
#ifdef XCB
    xcb_destroy_window(connection, window);
	xcb_disconnect(connection);
#endif
    if (deviceHandle != VK_NULL_HANDLE) {
        if (fontSampler != VK_NULL_HANDLE) {
            vkDestroySampler(deviceHandle, fontSampler, nullptr);
            fontSampler = VK_NULL_HANDLE;
        }
        if (fontView != VK_NULL_HANDLE) {
            vkDestroyImageView(deviceHandle, fontView, nullptr);
            fontView = VK_NULL_HANDLE;
        }
        if (fontImage != VK_NULL_HANDLE) {
            vkDestroyImage(deviceHandle, fontImage, nullptr);
            fontImage = VK_NULL_HANDLE;
        }
        if (fontMemory != VK_NULL_HANDLE) {
            vkFreeMemory(deviceHandle, fontMemory, nullptr);
            fontMemory = VK_NULL_HANDLE;
        }
    }
    ImGui::SaveIniSettingsToDisk("config.ini");
    ImNodes::DestroyContext();
    ImPlot3D::DestroyContext();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
};

#ifdef XCB
void Gui::initWindow(){
    initxcbConnection();
    setupWindow();
}
#endif

std::string Gui::getWindowTitle() const {
	std::string windowTitle =  title; 
	return windowTitle;
}

void Gui::prepareImGui(){
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImPlot3D::CreateContext();
    ImNodes::CreateContext();
    logs("[+] Created ImX context!");

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.IniFilename = nullptr; // Manual ini load/save only, no periodic autosave.
    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = false;
    ImFont *font = io.Fonts->AddFontFromMemoryTTF((void *)sansFlex_ttf,
           static_cast<int>(sansFlex_ttf_size), static_cast<float>(ui.fontSize), &fontConfig);
    float tighten = 0.92f; // <1.0 tightens spacing
    for (ImFontGlyph& g : font->Glyphs)
        g.AdvanceX *= tighten;
    if (font != nullptr) { io.FontDefault = font; }
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.FontGlobalScale = 1.0f;
    ui.installPersistentSettings();
    ImGui::LoadIniSettingsFromDisk("config.ini");
    ui.setStyle();
}

void Gui::refreshFontResources(VulkanDevice vulkanDevice, VkDescriptorSet descriptorSet){
    ImGuiIO &io = ImGui::GetIO();
    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = false;
    io.Fonts->Clear();
    ImFont* font = io.Fonts->AddFontFromMemoryTTF((void*)sansFlex_ttf,
            static_cast<int>(sansFlex_ttf_size), static_cast<float>(ui.fontSize), &fontConfig);
    float tighten = 0.92f; // <1.0 tightens spacing
    for (ImFontGlyph& g : font->Glyphs)
        g.AdvanceX *= tighten;
    if (font != nullptr) {
        io.FontDefault = font;
    }
    io.FontGlobalScale = 1.0f;
    io.Fonts->Build();

    unsigned char *fontData = nullptr;
    int texWidth = 0;
    int texHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    VkDeviceSize uploadSize = texWidth * texHeight * 4 * sizeof(char);

    if (fontSampler != VK_NULL_HANDLE) {
        vkDestroySampler(vulkanDevice.logicalDevice, fontSampler, nullptr);
        fontSampler = VK_NULL_HANDLE;
    }
    if (fontView != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkanDevice.logicalDevice, fontView, nullptr);
        fontView = VK_NULL_HANDLE;
    }
    if (fontImage != VK_NULL_HANDLE) {
        vkDestroyImage(vulkanDevice.logicalDevice, fontImage, nullptr);
        fontImage = VK_NULL_HANDLE;
    }
    if (fontMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice.logicalDevice, fontMemory, nullptr);
        fontMemory = VK_NULL_HANDLE;
    }

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
    VK_CHECK(vkBindImageMemory(vulkanDevice.logicalDevice, fontImage, fontMemory, 0));

    VkImageViewCreateInfo imageViewCreateInfo {};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image = fontImage;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(vulkanDevice.logicalDevice, &imageViewCreateInfo, nullptr, &fontView));

    VulkanBuffer stagingBuffer;
    VK_CHECK(vulkanDevice.createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                &stagingBuffer, uploadSize, nullptr));
    stagingBuffer.map(VK_WHOLE_SIZE, 0);
    memcpy(stagingBuffer.mapped, fontData, uploadSize);
    stagingBuffer.unmap();

    VkCommandBuffer copyCmd = vulkanDevice.createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, vulkanDevice.graphicsCommandPool, true);
    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.layerCount = 1;
    Gpu::setImageLayout(copyCmd, fontImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            subresourceRange, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy bufferCopyRegion = {};
    bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bufferCopyRegion.imageSubresource.layerCount = 1;
    bufferCopyRegion.imageExtent.width = texWidth;
    bufferCopyRegion.imageExtent.height = texHeight;
    bufferCopyRegion.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(copyCmd, stagingBuffer.buffer, fontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

    Gpu::setImageLayout(copyCmd, fontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vulkanDevice.flushCommandBuffer(copyCmd, vulkanDevice.graphicsQueue, vulkanDevice.graphicsCommandPool, true);
    stagingBuffer.destroy();

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
    vkCreateSampler(vulkanDevice.logicalDevice, &samplerCreateInfo, nullptr, &fontSampler);

    VkDescriptorImageInfo fontDescriptorImageInfo {};
    fontDescriptorImageInfo.sampler = fontSampler;
    fontDescriptorImageInfo.imageView = fontView;
    fontDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet fontWriteDescriptorSet {};
    fontWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    fontWriteDescriptorSet.dstSet = descriptorSet;
    fontWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    fontWriteDescriptorSet.dstBinding = 0;
    fontWriteDescriptorSet.pImageInfo = &fontDescriptorImageInfo;
    fontWriteDescriptorSet.descriptorCount = 1;

    vkUpdateDescriptorSets(vulkanDevice.logicalDevice, 1, &fontWriteDescriptorSet, 0, nullptr);
    io.Fonts->TexID = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(descriptorSet));
    ui.fontSizeDirty = false;
}

// initialize all vulkan resources used by the UI
void Gui::initResources(VulkanDevice vulkanDevice, VkRenderPass renderPass, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet descriptorSet){
    deviceHandle = vulkanDevice.logicalDevice;
    ui.backgroundShader.destroyResources(true, vulkanDevice.logicalDevice, descriptorPool);
    std::strncpy(ui.deviceName, vulkanDevice.deviceProperties.deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
    ui.deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1] = '\0';
    ui.vendorID = vulkanDevice.deviceProperties.vendorID;
    ui.deviceID = vulkanDevice.deviceProperties.deviceID;
    ui.deviceType = vulkanDevice.deviceProperties.deviceType;
    ui.driverVersion = vulkanDevice.deviceProperties.driverVersion;
    ui.apiVersion = vulkanDevice.deviceProperties.apiVersion;

    refreshFontResources(vulkanDevice, descriptorSet);

    ui.videoSource.texture = static_cast<ImTextureID>(0);
    ui.videoSource.textureSize = {0, 0};

    ui.backgroundShader.initShader({width, height}, false, (uint32_t*)background_vert_spv, background_vert_spv_size, 
            (uint32_t*)background_frag_spv, background_frag_spv_size, "background.frag");
    ui.backgroundShader.createResources(vulkanDevice, {width, height}, 
            descriptorPool, descriptorSetLayout);

    // Pipeline layout & Push constants for UI rendering
    VkPushConstantRange pushConstantRange {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(imguiPushConst);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo {};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
    VK_CHECK(vkCreatePipelineLayout(vulkanDevice.logicalDevice, &pipelineLayoutCreateInfo, nullptr, &imguiPipelineLayout));
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

    VkPipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo{};
    pipelineDynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    pipelineDynamicStateCreateInfo.pDynamicStates = NULL;
    pipelineDynamicStateCreateInfo.dynamicStateCount = 0;
    pipelineDynamicStateCreateInfo.flags = 0;

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

    VkGraphicsPipelineCreateInfo pipelineCreateInfo {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.layout = imguiPipelineLayout;
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

    pipelineCreateInfo.pVertexInputState = &pipelineVertexInputStateCreateInfo;

    shaderStages[0] = VulkanShader::loadShaderFromMemory(ui_vert_spv, ui_vert_spv_size, VK_SHADER_STAGE_VERTEX_BIT, vulkanDevice.logicalDevice);
    shaderStages[1] = VulkanShader::loadShaderFromMemory(ui_frag_spv, ui_frag_spv_size, VK_SHADER_STAGE_FRAGMENT_BIT, vulkanDevice.logicalDevice);

    vkCreateGraphicsPipelines(vulkanDevice.logicalDevice, NULL, 1, &pipelineCreateInfo, nullptr, &imguiGraphicsPipeline);
    logs("[+] Created Graphics Gui Pipeline ");
}

void Gui::buildCommandBuffers(VulkanDevice vulkanDevice, VkRenderPass renderPass, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet descriptorSet,
        std::vector<VkFramebuffer> frameBuffers, std::vector<VkCommandBuffer> drawCmdBuffers, uint32_t& idx){
    VkClearValue clearValues[1];
    clearValues[0].color = {{0.0f, 0.00f, 0.00f, 1.0f}};

    VkRenderPassBeginInfo renderPassBeginInfo {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width = width;
    renderPassBeginInfo.renderArea.extent.height = height;
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = clearValues;

    ui.build();
    if (ui.fontSizeDirty) refreshFontResources(vulkanDevice, descriptorSet);
    if (ui.backgroundShader.dirty)
        ui.backgroundShader.createResources(vulkanDevice, ui.backgroundShader.extent, descriptorPool, descriptorSetLayout);

    updateBuffers(vulkanDevice);

    VkCommandBufferBeginInfo cmdBufferBeginInfo {};
    cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    renderPassBeginInfo.framebuffer = frameBuffers[idx];
    VK_CHECK(vkBeginCommandBuffer(drawCmdBuffers[idx], &cmdBufferBeginInfo));
    ui.backgroundShader.recordShaderPass(drawCmdBuffers[idx]);
    vkCmdBeginRenderPass(drawCmdBuffers[idx], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindDescriptorSets(drawCmdBuffers[idx], VK_PIPELINE_BIND_POINT_GRAPHICS, imguiPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    drawFrame(drawCmdBuffers[idx], descriptorSet);
    vkCmdEndRenderPass(drawCmdBuffers[idx]);
    VK_CHECK(vkEndCommandBuffer(drawCmdBuffers[idx]));
}

void Gui::updateBuffers(VulkanDevice vulkanDevice){
    static VkDeviceSize dedicatedVertexSize = 8 * 1024 * 1024;
    static VkDeviceSize dedicatedIndexSize  = 8 * 1024 * 1024;
    ImDrawData *imDrawData = ImGui::GetDrawData();

    // Note: Alignment is done inside buffer creation
    VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
    VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

    if ((vertexBufferSize == 0) || (indexBufferSize == 0)) { 
        logs("[!] updateBuffers: No draw data available (vertexBufferSize=" << vertexBufferSize << ", indexBufferSize=" << indexBufferSize << ")");
        return; 
    }

    while(vertexBufferSize > dedicatedVertexSize) dedicatedVertexSize = dedicatedVertexSize * 2;
    while(indexBufferSize > dedicatedIndexSize) dedicatedIndexSize = dedicatedIndexSize * 2;

    vertexBufferSize = dedicatedVertexSize;
    indexBufferSize = dedicatedIndexSize;

    // Vertex Buffer
    if (((vertexBuffer.buffer == VK_NULL_HANDLE) || (vertexCount != imDrawData->TotalVtxCount)) && (vertexBuffer.size < vertexBufferSize)) {
      vertexBuffer.unmap();
      vertexBuffer.destroy();
      VK_CHECK(vulkanDevice.createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vertexBuffer, vertexBufferSize, nullptr));
      vertexCount = imDrawData->TotalVtxCount;
      vertexBuffer.map(VK_WHOLE_SIZE, 0);
    }

    // Index buffer
    if (((indexBuffer.buffer == VK_NULL_HANDLE) || (indexCount < imDrawData->TotalIdxCount)) && (indexBuffer.size < indexBufferSize)) {
      indexBuffer.unmap();
      indexBuffer.destroy();
      VK_CHECK(vulkanDevice.createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &indexBuffer, indexBufferSize, 0));
      indexCount = imDrawData->TotalIdxCount;
      indexBuffer.map(VK_WHOLE_SIZE, 0);
    }

    // Upload data
    ImDrawVert *vtxDst = (ImDrawVert *)vertexBuffer.mapped;
    ImDrawIdx *idxDst = (ImDrawIdx *)indexBuffer.mapped;

    for (int n = 0; n < imDrawData->CmdListsCount; n++) {
      const ImDrawList *cmd_list = imDrawData->CmdLists[n];
      memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
      memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
      vtxDst += cmd_list->VtxBuffer.Size;
      idxDst += cmd_list->IdxBuffer.Size;
    }
}

void Gui::drawFrame(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet){
    ImGuiIO &io = ImGui::GetIO();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, imguiGraphicsPipeline);
    VkViewport viewport {};
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = 0.0;
    viewport.maxDepth = 1.0;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    imguiPushConst.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
    imguiPushConst.translate = glm::vec2(-1.0f);
    imguiPushConst.invScreenSize = glm::vec2(1.0f / io.DisplaySize.x, 1.0f / io.DisplaySize.y);
    imguiPushConst.whitePixel = glm::vec2(io.Fonts->TexUvWhitePixel.x, io.Fonts->TexUvWhitePixel.y);
    imguiPushConst.gradTop = glm::vec4(1.00f, 1.00f, 1.00f, 1.00f);
    imguiPushConst.gradBottom = glm::vec4(1.00f, 1.00f, 1.00f, 1.00f);
    imguiPushConst.u_time = (float)ImGui::GetTime();
    vkCmdPushConstants(commandBuffer, imguiPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstBlock), &imguiPushConst);

    // Render commands
    ImDrawData *imDrawData = ImGui::GetDrawData();
    int32_t vertexOffset = 0;
    int32_t indexOffset = 0;

    if (imDrawData->CmdListsCount > 0) {
      VkDeviceSize offsets[1] = {0};
      vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer.buffer, offsets);
      vkCmdBindIndexBuffer(commandBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);
      VkDescriptorSet boundSet = VK_NULL_HANDLE;
      for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
        const ImDrawList *cmd_list = imDrawData->CmdLists[i];
        for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {
          const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[j];
          VkDescriptorSet textureSet = descriptorSet;
          if (pcmd->TextureId != 0) {
              textureSet = reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(pcmd->TextureId));
          }
          if (textureSet == VK_NULL_HANDLE) {
              textureSet = descriptorSet;
          }
          if (textureSet != boundSet) {
              vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, imguiPipelineLayout, 0, 1, &textureSet, 0, nullptr);
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
    xcb_atom_t deleteAtom = (atom_wm_delete_window != nullptr) ? (*atom_wm_delete_window).atom : XCB_ATOM_NONE;
    inputs.handleXcbEvent(event, quit, deleteAtom);
}
#endif

#ifdef WIN

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((DPI_AWARENESS_CONTEXT)-3)
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

static void dpiAware(){
    static bool applied = false;
    if (applied){
        return;
    }
    applied = true;

    HMODULE user32 = LoadLibraryA("user32.dll");
    BOOL awarenessSet = FALSE;
    if (user32){
        typedef BOOL (WINAPI *SetProcessDpiAwarenessContextFn)(DPI_AWARENESS_CONTEXT);
        typedef BOOL (WINAPI *SetProcessDPIAwareFn)(void);
        SetProcessDpiAwarenessContextFn setContext = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext){
            awarenessSet = setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            if (!awarenessSet){
                awarenessSet = setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
            }
            if (awarenessSet){
                logs("[+] Enabled per-monitor DPI awareness via SetProcessDpiAwarenessContext");
                FreeLibrary(user32);
                return;
            }
        }

        HMODULE shcore = LoadLibraryA("shcore.dll");
        if (shcore){
            typedef HRESULT (WINAPI *SetProcessDpiAwarenessFn)(PROCESS_DPI_AWARENESS);
            SetProcessDpiAwarenessFn setAwareness = reinterpret_cast<SetProcessDpiAwarenessFn>(
                GetProcAddress(shcore, "SetProcessDpiAwareness"));
            if (setAwareness){
                HRESULT hr = setAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
                if (hr == S_OK || hr == E_ACCESSDENIED){
                    logs("[+] Enabled per-monitor DPI awareness via SetProcessDpiAwareness");
                    FreeLibrary(shcore);
                    FreeLibrary(user32);
                    return;
                }
            }
            FreeLibrary(shcore);
        }

        SetProcessDPIAwareFn setLegacy = reinterpret_cast<SetProcessDPIAwareFn>(
            GetProcAddress(user32, "SetProcessDPIAware"));
        if (setLegacy && setLegacy()){
            logs("[+] Enabled system DPI awareness via SetProcessDPIAware");
        }
        FreeLibrary(user32);
    }
}

LRESULT CALLBACK Gui::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
    Gui* pThis = nullptr;

    if (uMsg == WM_NCCREATE) {
        auto createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<Gui*>(createStruct->lpCreateParams);
        if (pThis) {
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            pThis->window = hWnd;
        }
    } else {
        pThis = reinterpret_cast<Gui*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->handleMessages(hWnd, uMsg, wParam, lParam);
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void Gui::initWindow(HINSTANCE hInstance){
    windowInstance = hInstance;
    dpiAware();
    WNDCLASSEX wndClass{};

    wndClass.cbSize = sizeof(WNDCLASSEX);
    wndClass.style = CS_HREDRAW | CS_VREDRAW;
    wndClass.lpfnWndProc = WndProc;
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.hInstance = windowInstance;
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wndClass.lpszMenuName = NULL;
    wndClass.lpszClassName = name.c_str();

    constexpr WORD PHOTON_APP_ICON_ID = 101;
    HICON largeIcon = LoadIcon(hInstance, MAKEINTRESOURCE(PHOTON_APP_ICON_ID));
    HICON smallIcon = static_cast<HICON>(LoadImage(
        hInstance,
        MAKEINTRESOURCE(PHOTON_APP_ICON_ID),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        0));

    if (!smallIcon) {smallIcon = largeIcon;}
    if (largeIcon)  {wndClass.hIcon = largeIcon;}
    if (smallIcon)  {wndClass.hIconSm = smallIcon;}

    if (!RegisterClassEx(&wndClass)){
        std::cout << "Could not register window class!\n";
        fflush(stdout);
        exit(1);
    }

    DWORD dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
    DWORD dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    RECT windowRect = {0L, 0L, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRectEx(&windowRect, dwStyle, FALSE, dwExStyle);

    window = CreateWindowEx(
        0,
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

    if (window) {
        if (largeIcon){SendMessage(window, WM_SETICON, ICON_BIG, (LPARAM)largeIcon);}
        if (smallIcon){SendMessage(window, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);}
    }

	const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
	const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 = 19;
	const DWORD DWMWA_SYSTEMBACKDROP_TYPE = 38; // Mica backdrop

	HMODULE dwm = LoadLibraryA("dwmapi.dll");
	if (dwm){
		using DwmSetWindowAttributeFunc = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
		auto DwmSetWindowAttributePtr = reinterpret_cast<DwmSetWindowAttributeFunc>(
			GetProcAddress(dwm, "DwmSetWindowAttribute"));
		if (DwmSetWindowAttributePtr){
			BOOL enabled = TRUE;
			DwmSetWindowAttributePtr(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled));
			DwmSetWindowAttributePtr(window, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1, &enabled, sizeof(enabled));
			DWORD backdrop = 2; // Mica
			DwmSetWindowAttributePtr(window, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
		}
		FreeLibrary(dwm);
	}

	HMODULE ux = LoadLibraryA("uxtheme.dll");
	if (ux){
		using SetWindowThemeFunc = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
		auto SetWindowThemePtr = reinterpret_cast<SetWindowThemeFunc>(
			GetProcAddress(ux, "SetWindowTheme"));
		if (SetWindowThemePtr){
			SetWindowThemePtr(window, L"DarkMode_Explorer", nullptr);
		}
		FreeLibrary(ux);
	}
    
    if (!window){
        std::cerr << "Could not create window!\n";
        fflush(stdout);
        return;
    }

    destWidth = width;
    destHeight = height;
    SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    SetFocus(window);
}

LRESULT Gui::handleMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
    return inputs.handleWin32Message(hWnd, uMsg, wParam, lParam, quit, destWidth, destHeight);
}
#endif
