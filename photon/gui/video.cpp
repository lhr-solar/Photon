#include "video.hpp"
#include "../gpu/vulkanDevice.hpp"
#include "../gpu/gpu.hpp"
#include "../engine/include.hpp"
#include "imgui.h"

void Video::initVideoFeedResources(VulkanDevice vulkanDevice, VkDescriptorPool descriptorPool, 
        VkDescriptorSetLayout descriptorSetLayout, VkSampler sampler){
#if defined(__linux__)
    if (!webcam.initialize("/dev/video0", 640, 480)) {
        logs("[!] Webcam: failed to initialize /dev/video0");
        texture = static_cast<ImTextureID>(0);
        textureSize = {0, 0};
        return;
    }

    videoSize = {webcam.width(), webcam.height()};
    if (videoSize.width == 0 || videoSize.height == 0) {
        logs("[!] Webcam: invalid extent " << videoSize.width << "x" << videoSize.height);
        webcam.shutdown();
        texture = static_cast<ImTextureID>(0);
        textureSize = {0, 0};
        return;
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = videoSize.width;
    imageInfo.extent.height = videoSize.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(vulkanDevice.logicalDevice, &imageInfo, nullptr, &image));

    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(vulkanDevice.logicalDevice, image, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = vulkanDevice.getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr);
    VK_CHECK(vkAllocateMemory(vulkanDevice.logicalDevice, &allocInfo, nullptr, &memory));
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
    textureSize = {videoSize.width, videoSize.height};

    stagingBufferSize = 0;
    frameData.clear();
    logs("[+] Webcam: initialized video feed " << videoSize.width << "x" << videoSize.height);
#else
    (void)vulkanDevice;
    ui.videoTexture = static_cast<ImTextureID>(0);
    ui.videoTextureSize = ImVec2(0.0f, 0.0f);
    logs("[!] Webcam: capture is only supported on Linux");
#endif
}

void Video::updateVideoFeed(VulkanDevice vulkanDevice){
#if defined(__linux__)
    if (!webcam.isAvailable()) { return; }
    if (image == VK_NULL_HANDLE) {
        return;
    }

    if (!webcam.captureFrame(frameData)) { return; }
    if (frameData.empty()) { return; }

    VkDeviceSize requiredSize = static_cast<VkDeviceSize>(frameData.size());
    if ((stagingBuffer.buffer == VK_NULL_HANDLE) || (requiredSize > stagingBufferSize)) {
        stagingBuffer.unmap();
        stagingBuffer.destroy();
        VK_CHECK(vulkanDevice.createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
            requiredSize,
            nullptr));
        stagingBufferSize = requiredSize;
    }

    VK_CHECK(stagingBuffer.map(requiredSize, 0));
    memcpy(stagingBuffer.mapped, frameData.data(), static_cast<size_t>(requiredSize));
    stagingBuffer.unmap();

    VkCommandBuffer copyCmd = vulkanDevice.createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, vulkanDevice.graphicsCommandPool, true);
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    VkImageLayout oldLayout = initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags srcStage = initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    Gpu::setImageLayout(copyCmd, image, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = videoSize.width;
    region.imageExtent.height = videoSize.height;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(copyCmd, stagingBuffer.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    Gpu::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
            range, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vulkanDevice.flushCommandBuffer(copyCmd, vulkanDevice.graphicsQueue, vulkanDevice.graphicsCommandPool, true);

    initialized = true;
    layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    textureSize = {videoSize.width, videoSize.height};
#else
    (void)vulkanDevice;
#endif
}

void Video::destroyVideoFeedResources(bool releaseDescriptorSet, VkDevice deviceHandle, VkDescriptorPool descriptorPool){
    webcam.shutdown();
    if (stagingBuffer.buffer != VK_NULL_HANDLE) {
        stagingBuffer.unmap();
        stagingBuffer.destroy();
        stagingBufferSize = 0;
    }

    if (releaseDescriptorSet && deviceHandle != VK_NULL_HANDLE && descriptorPool != VK_NULL_HANDLE && descriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(deviceHandle, descriptorPool, 1, &descriptorSet);
        descriptorSet = VK_NULL_HANDLE;
    }

    if (deviceHandle != VK_NULL_HANDLE) {
        if (view != VK_NULL_HANDLE) { vkDestroyImageView(deviceHandle, view, nullptr); }
        if (image != VK_NULL_HANDLE) { vkDestroyImage(deviceHandle, image, nullptr); }
        if (memory != VK_NULL_HANDLE) { vkFreeMemory(deviceHandle, memory, nullptr); }
    }

    view = VK_NULL_HANDLE;
    image = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    videoSize = {0, 0};
    layout = VK_IMAGE_LAYOUT_UNDEFINED;
    initialized = false;

    frameData.clear();
    texture= static_cast<ImTextureID>(0);
    textureSize = {0, 0};
}
