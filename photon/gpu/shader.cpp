#include "shader.hpp"
#include "vulkan_core.h"

#include <vector>

static void destroyFrame(Shader& shader, uint32_t index){
    if (index >= shader.frames.size()) return;
    shaderFrame& frame = shader.frames[index];
    if (frame.framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(shader.device, frame.framebuffer, NULL);
    if (frame.view != VK_NULL_HANDLE) vkDestroyImageView(shader.device, frame.view, NULL);
    if (frame.image != VK_NULL_HANDLE) shader.gpu->destroyImage(frame.image);
    if (frame.imageMemory != VK_NULL_HANDLE) shader.gpu->freeMemory(frame.imageMemory);
    frame.image = VK_NULL_HANDLE;
    frame.view = VK_NULL_HANDLE;
    frame.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    frame.imageMemory = VK_NULL_HANDLE;
    frame.framebuffer = VK_NULL_HANDLE;
    frame.initialized = false;
}

static void initFrame(Shader& shader, GPU& gpu, uint32_t index){
    if (index >= shader.frames.size()) return;
    shaderFrame& frame = shader.frames[index];

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = frame.extent.width;
    imageInfo.extent.height = frame.extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    gpu.createImage(imageInfo, &frame.image);

    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(shader.device, frame.image, &memReqs);
    VkMemoryAllocateInfo imageAllocInfo{};
    imageAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageAllocInfo.allocationSize = memReqs.size;
    imageAllocInfo.memoryTypeIndex = gpu.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    gpu.allocateMemory(imageAllocInfo, &frame.imageMemory);
    vkBindImageMemory(shader.device, frame.image, frame.imageMemory, 0);
    frame.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = frame.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(shader.device, &viewInfo, nullptr, &frame.view);

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = shader.renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &frame.view;
    framebufferInfo.width = frame.extent.width;
    framebufferInfo.height = frame.extent.height;
    framebufferInfo.layers = 1;
    vkCreateFramebuffer(shader.device, &framebufferInfo, nullptr, &frame.framebuffer);

    if (frame.descriptorSet == VK_NULL_HANDLE){
        VkDescriptorSetAllocateInfo descriptorAlloc{};
        descriptorAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorAlloc.descriptorPool = shader.descriptorPool;
        descriptorAlloc.descriptorSetCount = 1;
        descriptorAlloc.pSetLayouts = &shader.descriptorSetLayout;
        gpu.allocateDescriptorSets(descriptorAlloc, &frame.descriptorSet);
    }

    VkDescriptorImageInfo imageDescriptor{};
    imageDescriptor.sampler = shader.sampler;
    imageDescriptor.imageView = frame.view;
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = frame.descriptorSet;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.pImageInfo = &imageDescriptor;
    vkUpdateDescriptorSets(shader.device, 1, &write, 0, nullptr);
    frame.texture = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(frame.descriptorSet));
}

void Shader::init(GPU& gpu,
    uint32_t* vertexShader, size_t vertexShaderSize,
    uint32_t* fragmentShader, size_t fragmentShaderSize){
    prepareInit(gpu, vertexShader, vertexShaderSize, fragmentShader, fragmentShaderSize);
    finishInit(gpu);
}

void Shader::prepareInit(GPU& gpu,
    uint32_t* vertexShader, size_t vertexShaderSize,
    uint32_t* fragmentShader, size_t fragmentShaderSize){
    this->gpu = &gpu;
    device = gpu.device;
    descriptorPool = gpu.descriptorPool;
    descriptorSetLayout = gpu.descriptorSetLayout;
    fragShader = fragmentShader;
    fragShaderSize = fragmentShaderSize;
    vertShader = vertexShader;
    vertShaderSize = vertexShaderSize;
    frameIndex = &gpu.frameIndex;
    fif = gpu.swapchainImages.size();

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
    vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pcRange;
    vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout);

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
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0] = gpu.loadShader(vertShader, vertShaderSize, vertShaderModule,
            VK_SHADER_STAGE_VERTEX_BIT, device);
    shaderStages[1] = gpu.loadShader(fragShader, fragShaderSize, fragShaderModule,
            VK_SHADER_STAGE_FRAGMENT_BIT, device);

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
    vkCreateGraphicsPipelines(device, NULL, 1, &pipelineInfo, nullptr, &pipeline);

    VkSamplerCreateInfo samplerCreateInfo{};
    samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCreateInfo.maxAnisotropy = 1.0f;
    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    vkCreateSampler(device, &samplerCreateInfo, nullptr, &sampler);
    partInitialized.store(true);
}

void Shader::finishInit(GPU& gpu) {
    if (!partInitialized.load() || initialized.load()) return;
    frames.assign(fif, {});
    for (uint32_t i = 0; i < fif; i++)
        initFrame(*this, gpu, i);
    initialized.store(true);
    partInitialized.store(false);
    dirty = false;
}

void Shader::dispatchInit(GPU& gpu,
    uint32_t* vertexShader, size_t vertexShaderSize,
    uint32_t* fragmentShader, size_t fragmentShaderSize) {
    gpuAsyncDispatches.fetch_add(1, std::memory_order_relaxed);
    const std::vector<uint32_t> vertexCopy = vertexShader != nullptr && vertexShaderSize != 0
        ? std::vector<uint32_t>(vertexShader, vertexShader + vertexShaderSize / sizeof(uint32_t))
        : std::vector<uint32_t>{};
    const std::vector<uint32_t> fragmentCopy = fragmentShader != nullptr && fragmentShaderSize != 0
        ? std::vector<uint32_t>(fragmentShader, fragmentShader + fragmentShaderSize / sizeof(uint32_t))
        : std::vector<uint32_t>{};
    std::thread([this, &gpu,
        vertexCopy = std::move(vertexCopy), vertexShaderSize,
        fragmentCopy = std::move(fragmentCopy), fragmentShaderSize]() {
        AsyncDispatchGuard guard{};
        prepareInit(gpu,
            vertexCopy.empty() ? nullptr : const_cast<uint32_t*>(vertexCopy.data()), vertexShaderSize,
            fragmentCopy.empty() ? nullptr : const_cast<uint32_t*>(fragmentCopy.data()), fragmentShaderSize);
    }).detach();
}

void Shader::render(GPU& gpu, VkCommandBuffer& commandBuffer){
    if (!initialized.load() && partInitialized.load()) finishInit(gpu);
    if (!initialized.load() || frames.empty() || frameIndex == nullptr) return;
    if(dirty) rebuild(gpu);
    shaderFrame& frame = frames[*frameIndex];
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;
    VkImageLayout oldLayout = frame.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags srcStage = frame.initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkClearValue clearValue{};
    clearValue.color = {{0.00f, 0.00f, 0.00f, 1.0f}};
    VkRenderPassBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = renderPass;
    beginInfo.framebuffer = frame.framebuffer;
    beginInfo.renderArea.offset = {0, 0};
    beginInfo.renderArea.extent = frame.extent;
    beginInfo.clearValueCount = 1;
    beginInfo.pClearValues = &clearValue;
    gpu.setImageLayout(commandBuffer, frame.image, oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            range, srcStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkViewport viewport{};
    viewport.width = static_cast<float>(frame.extent.width);
    viewport.height = static_cast<float>(frame.extent.height);
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = frame.extent;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    pc.resolution = glm::vec2(static_cast<float>(frame.extent.width), static_cast<float>(frame.extent.height));
    pc.u_time     = (float)ImGui::GetTime();
    pc.pad        = 0.0f;
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
    gpu.setImageLayout(commandBuffer, frame.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    frame.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    frame.initialized = true;
}

void Shader::rebuild(GPU& gpu){
    if (!initialized.load() || frames.empty() || frameIndex == nullptr) return;
    const uint32_t index = *frameIndex;
    destroyFrame(*this, index);
    initFrame(*this, gpu, index);
    dirty = false;
}

void Shader::destroy(){
    if (device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device);
    for (uint32_t i = 0; i < frames.size(); i++) {
        const VkDescriptorSet descriptorSet = frames[i].descriptorSet;
        destroyFrame(*this, i);
        if (descriptorSet != VK_NULL_HANDLE)
            gpu->freeDescriptorSets(descriptorPool, 1, &descriptorSet);
    }
    vkDestroySampler(device, sampler, NULL);
    vkDestroyRenderPass(device, renderPass, NULL);
    vkDestroyShaderModule(device, fragShaderModule, NULL);
    vkDestroyShaderModule(device, vertShaderModule, NULL);
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    frames.clear();
    initialized.store(false);
    partInitialized.store(false);
    device = VK_NULL_HANDLE;
}
