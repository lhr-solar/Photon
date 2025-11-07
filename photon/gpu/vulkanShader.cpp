#include "vulkanShader.hpp"
#include "gpu.hpp"
#include "imgui.h"
#include <array>
#include <string>

void VulkanShader::initShader(VkExtent2D extent, bool runtimeFrag, 
            uint32_t* vertexShader, size_t vertexShaderSize, 
            uint32_t* fragmentShader, size_t fragmentShaderSize, std::string fragName){
    this->extent = extent;
    this->runtimeFrag = runtimeFrag;
    this->vertexShader = vertexShader;
    this->vertexShaderSize = vertexShaderSize;
    if(!runtimeFrag){
        this->fragmentShader = fragmentShader;
        this->fragmentShaderSize = fragmentShaderSize;
    } else {
        this->fragmentShaderName = fragName;
    }
}

void VulkanShader::createResources(VulkanDevice vulkanDevice, VkExtent2D extent, VkDescriptorPool descriptorPool, 
            VkDescriptorSetLayout descriptorSetLayout, VkPipelineCache pipelineCache, VkSampler sampler){
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = extent.width;
    imageInfo.extent.height = extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(vulkanDevice.logicalDevice, &imageInfo, nullptr, &image));

    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(vulkanDevice.logicalDevice, image, &memReqs);
    VkMemoryAllocateInfo imageAllocInfo{};
    imageAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageAllocInfo.allocationSize = memReqs.size;
    imageAllocInfo.memoryTypeIndex = vulkanDevice.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr);
    VK_CHECK(vkAllocateMemory(vulkanDevice.logicalDevice, &imageAllocInfo, nullptr, &memory));
    VK_CHECK(vkBindImageMemory(vulkanDevice.logicalDevice, image, memory, 0));
    layout = VK_IMAGE_LAYOUT_UNDEFINED;
    initialized = false;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(vulkanDevice.logicalDevice, &viewInfo, nullptr, &view));


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
    VK_CHECK(vkCreateRenderPass(vulkanDevice.logicalDevice, &renderPassInfo, nullptr, &renderPass));

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &view;
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;
    VK_CHECK(vkCreateFramebuffer(vulkanDevice.logicalDevice, &framebufferInfo, nullptr, &framebuffer));

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pcRange;
    VK_CHECK(vkCreatePipelineLayout(vulkanDevice.logicalDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout));
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
    shaderStages[0] = Gpu::loadShader(vertexShader, vertexShaderSize, VK_SHADER_STAGE_VERTEX_BIT, vulkanDevice.logicalDevice);
    if(runtimeFrag == false){
        shaderStages[1] = Gpu::loadShader(fragmentShader, fragmentShaderSize, VK_SHADER_STAGE_FRAGMENT_BIT, vulkanDevice.logicalDevice);
    } else {
        shaderStages[1] = Gpu::loadShaderFromPath(fragmentShaderName, VK_SHADER_STAGE_FRAGMENT_BIT, vulkanDevice.logicalDevice);
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
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

    VK_CHECK(vkCreateGraphicsPipelines(vulkanDevice.logicalDevice, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline));

    if (descriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo descriptorAlloc{};
        descriptorAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorAlloc.descriptorPool = descriptorPool;
        descriptorAlloc.descriptorSetCount = 1;
        descriptorAlloc.pSetLayouts = &descriptorSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(vulkanDevice.logicalDevice, &descriptorAlloc, &descriptorSet));
    }

    VkDescriptorImageInfo imageDescriptor{};
    imageDescriptor.sampler = sampler;
    imageDescriptor.imageView = view;
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.pImageInfo = &imageDescriptor;
    vkUpdateDescriptorSets(vulkanDevice.logicalDevice, 1, &write, 0, nullptr);
    texture = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(descriptorSet));
    dirty = false;
}

void VulkanShader::destroyResources(bool releaseDescriptor, VkDevice deviceHandle, VkDescriptorPool descriptorPool){
    if (releaseDescriptor&& descriptorSet != VK_NULL_HANDLE && deviceHandle != VK_NULL_HANDLE && descriptorPool != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(deviceHandle, descriptorPool, 1, &descriptorSet);
        descriptorSet = VK_NULL_HANDLE;
    }

    if (deviceHandle != VK_NULL_HANDLE) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(deviceHandle, pipeline, nullptr);
        }
        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(deviceHandle, pipelineLayout, nullptr);
        }
        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(deviceHandle, renderPass, nullptr);
        }
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(deviceHandle, framebuffer, nullptr);
        }
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(deviceHandle, view, nullptr);
        }
        if (image != VK_NULL_HANDLE) {
            vkDestroyImage(deviceHandle, image, nullptr);
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(deviceHandle, memory, nullptr);
        }
    }

    extent = {0, 0};
    pipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    renderPass = VK_NULL_HANDLE;
    framebuffer = VK_NULL_HANDLE;
    view = VK_NULL_HANDLE;
    image = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    layout = VK_IMAGE_LAYOUT_UNDEFINED;
    initialized = false;
}

void VulkanShader::recordShaderPass(VkCommandBuffer commandBuffer){
    if (pipeline == VK_NULL_HANDLE) { return; }
    if (extent.width == 0 || extent.height == 0) { return; }

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    VkImageLayout oldLayout = initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags srcStage = initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    Gpu::setImageLayout(commandBuffer, image, oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, range, srcStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkClearValue clearValue{};
    clearValue.color = {{0.05f, 0.04f, 0.10f, 1.0f}};

    VkRenderPassBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = renderPass;
    beginInfo.framebuffer = framebuffer;
    beginInfo.renderArea.offset = {0, 0};
    beginInfo.renderArea.extent = extent;
    beginInfo.clearValueCount = 1;
    beginInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    scissor.offset = {0, 0};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    pc.resolution = glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
    pc.u_time     = (float)ImGui::GetTime();
    pc.pad        = 0.0f;
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    Gpu::setImageLayout(commandBuffer, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
            range, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    initialized = true;
    layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
