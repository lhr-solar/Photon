#include "ui.hpp"
#include "../engine/include.hpp"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include "videoElement.hpp"
#include "videoDecoder.hpp"

void UI::build(){
    ImGui::NewFrame();
    //videoDecoder::ffmpeg_init_once();
    customBackground();
    ImPlot::ShowDemoWindow();
    ImPlot3D::ShowDemoWindow();
    fpsWindow();
    customShaderWindow();
    showVideoDisplay();
    showVideoWindow();
    networkSamplePlot();

    ImGui::Render();
}

static Video* gVideoPlayer = nullptr;

void UI::showVideoWindow() {
    ImGui::Begin("Video Player");
    
    // Static resources for video texture
    static bool videoInitialized = false;
    static VkDescriptorSet videoTextureDescriptor = VK_NULL_HANDLE;
    static VkImageView videoImageView = VK_NULL_HANDLE;
    static VkImage videoImage = VK_NULL_HANDLE;
    static VkDeviceMemory videoMemory = VK_NULL_HANDLE;
    static VkSampler videoSampler = VK_NULL_HANDLE;
    static uint32_t lastWidth = 0;
    static uint32_t lastHeight = 0;
    
    // Initialize video player once
    if (!videoInitialized) {
        if (gVideoPlayer == nullptr) {
            gVideoPlayer = new Video();
        }
        
        if (gVideoPlayer->open("./mp4Videos/F1POV.mp4")) {
            videoInitialized = true;
            logs("[+] Video player initialized successfully\n");
        } else {
            ImGui::Text("Failed to open video: ./mp4Videos/F1POV.mp4");
            ImGui::Text("Check that the file exists and is a valid video format");
            ImGui::End();
            return;
        }
    }
    
    // Update frame (decode next frame)
    if (!gVideoPlayer->updateFrame()) {
        ImGui::Text("End of video or decode error");
        ImGui::End();
        return;
    }
    
    // Draw the current frame
    const frame& current = gVideoPlayer->getFrame();
    if (current.isValid()) {
        uint32_t width = gVideoPlayer->width();
        uint32_t height = gVideoPlayer->height();
        
        // Recreate texture if dimensions changed or first frame
        if (videoImage == VK_NULL_HANDLE || lastWidth != width || lastHeight != height) {
            // Clean up old resources if they exist
            if (videoImageView != VK_NULL_HANDLE) {
                vkDestroyImageView(device, videoImageView, nullptr);
                videoImageView = VK_NULL_HANDLE;
            }
            if (videoImage != VK_NULL_HANDLE) {
                vkDestroyImage(device, videoImage, nullptr);
                videoImage = VK_NULL_HANDLE;
            }
            if (videoMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, videoMemory, nullptr);
                videoMemory = VK_NULL_HANDLE;
            }
            if (videoSampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, videoSampler, nullptr);
                videoSampler = VK_NULL_HANDLE;
            }
            
            // Create new texture
            logs("[+] Creating video texture %ux%u\n", width, height);
            createVideoTexture(width, height, videoImage, videoMemory, videoImageView, videoSampler);
            
            // Create descriptor set
            videoTextureDescriptor = createDescriptorSetForTexture(videoSampler, videoImageView);
            
            lastWidth = width;
            lastHeight = height;
        }
        
        // Upload current frame data to GPU
        uploadVideoFrameToTexture(current, videoImage, width, height);
        
        // Display the video frame
        if (videoTextureDescriptor != VK_NULL_HANDLE) {
            ImGui::Image((ImTextureID)(intptr_t)videoTextureDescriptor, 
                        ImVec2((float)width, (float)height));
            
            // Display frame info
            ImGui::Text("Resolution: %dx%d", width, height);
            ImGui::Text("Timestamp: %.3f seconds", current.timestamp);
            ImGui::Text("Stride: %d bytes", current.stride);
            
            // Add playback controls
            ImGui::Separator();
            if (ImGui::Button("Restart")) {
                // Close and reopen to restart
                gVideoPlayer->close();
                videoInitialized = false;
            }
        } else {
            ImGui::Text("Texture descriptor not ready");
        }
    } else {
        ImGui::Text("No valid frame data");
    }
    
    ImGui::End();
}
// ============================================================================
// Vulkan Helper Function Implementations
// ============================================================================

void UI::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory");
    }
    
    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

uint32_t UI::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    throw std::runtime_error("Failed to find suitable memory type");
}

VkCommandBuffer UI::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
    
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    return commandBuffer;
}

void UI::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);
    
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

VkDescriptorSet UI::createDescriptorSetForTexture(VkSampler sampler, VkImageView imageView) {
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = imguiDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &imguiDescriptorSetLayout;
    
    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set for video texture");
    }
    
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;
    
    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    
    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    
    return descriptorSet;
}

void UI::createVideoTexture(uint32_t width, uint32_t height,
                            VkImage& outImage, VkDeviceMemory& outMemory,
                            VkImageView& outView, VkSampler& outSampler) {
    
    logs("[+] createVideoTexture START: size=%ux%u\n", width, height);
    
    // Pick a supported 4-byte format (prefer RGBA, fall back to BGRA)
    VkFormat candidates[] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM };
    VkFormat chosenFormat = VK_FORMAT_UNDEFINED;
    
    for (VkFormat fmt : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, fmt, &props);
        
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) &&
            (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
            chosenFormat = fmt;
            logs("[+]] Selected format: %d (supports transfer & sampling)\n", (int)chosenFormat);
            break;
        }
    }

    if (chosenFormat == VK_FORMAT_UNDEFINED) {
        logs("[!] ERROR: No suitable RGBA format found for video texture.\n");
        throw std::runtime_error("No suitable video texture format supported by device");
    }

    // Store chosen format for later use
    this->videoTextureFormat = chosenFormat;

    // Create image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = chosenFormat;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    VkResult result = vkCreateImage(device, &imageInfo, nullptr, &outImage);
    if (result != VK_SUCCESS) {
        logs("[!] ERROR: vkCreateImage FAILED with result %d\n", result);
        throw std::runtime_error("Failed to create video texture image");
    }
    logs("[+] vkCreateImage OK -> image=%p\n", (void*)outImage);
    
    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, outImage, &memReqs);
    logs("[+] Image memory requirements: size=%llu, alignment=%llu, memTypeBits=0x%X\n", 
           (unsigned long long)memReqs.size, 
           (unsigned long long)memReqs.alignment,
           memReqs.memoryTypeBits);
    
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    result = vkAllocateMemory(device, &allocInfo, nullptr, &outMemory);
    if (result != VK_SUCCESS) {
        logs("[!] vkAllocateMemory FAILED with result %d\n", result);
        vkDestroyImage(device, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to allocate video texture memory");
    }
    logs("[+] vkAllocateMemory OK\n");
    
    result = vkBindImageMemory(device, outImage, outMemory, 0);
    if (result != VK_SUCCESS) {
        logs("[!] ERROR: vkBindImageMemory FAILED with result %d\n", result);
        vkFreeMemory(device, outMemory, nullptr);
        vkDestroyImage(device, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to bind video texture memory");
    }
    logs("[+] vkBindImageMemory OK\n");
    
    // Create image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = outImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = chosenFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    result = vkCreateImageView(device, &viewInfo, nullptr, &outView);
    if (result != VK_SUCCESS) {
        logs("[!] ERROR: vkCreateImageView FAILED with result %d\n", result);
        vkFreeMemory(device, outMemory, nullptr);
        vkDestroyImage(device, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to create video texture image view");
    }
    logs("[+] vkCreateImageView OK -> view=%p\n", (void*)outView);
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    
    result = vkCreateSampler(device, &samplerInfo, nullptr, &outSampler);
    if (result != VK_SUCCESS) {
        logs("[!] ERROR: vkCreateSampler FAILED with result %d\n", result);
        vkDestroyImageView(device, outView, nullptr);
        vkFreeMemory(device, outMemory, nullptr);
        vkDestroyImage(device, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        outView = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to create video texture sampler");
    }
    logs("[+] vkCreateSampler OK -> sampler=%p\n", (void*)outSampler);
    
    // Transition image layout to shader read optimal
    logs("[+] transitionImageLayout: UNDEFINED -> SHADER_READ for image %p\n", (void*)outImage);
    
    try {
        transitionImageLayout(outImage, chosenFormat,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        logs("[+] transitionImageLayout complete\n");
    } catch (const std::exception& e) {
        logs("[!] ERROR: transitionImageLayout failed: %s\n", e.what());
        vkDestroySampler(device, outSampler, nullptr);
        vkDestroyImageView(device, outView, nullptr);
        vkFreeMemory(device, outMemory, nullptr);
        vkDestroyImage(device, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        outView = VK_NULL_HANDLE;
        outSampler = VK_NULL_HANDLE;
        throw;
    }
    
    logs("[+] createVideoTexture success\n");
}
void UI::uploadVideoFrameToTexture(const frame& videoFrame, VkImage image,
                                   uint32_t width, uint32_t height) {
    if (!videoFrame.isValid()) {
        logs("[!] uploadVideoFrameToTexture: Invalid frame\n");
        return;
    }
    
    if (videoFrame.width != width || videoFrame.height != height) {
        logs("[!] uploadVideoFrameToTexture: Size mismatch - frame=%dx%d, expected=%dx%d\n",
               videoFrame.width, videoFrame.height, width, height);
        return;
    }
    
    // Use the frame's actual stride (includes alignment padding)
    int bpp = 4; // RGBA
    VkDeviceSize imageSize = (VkDeviceSize)videoFrame.stride * (VkDeviceSize)height;
    
    logs("[+] uploadVideoFrameToTexture: image=%p width=%u height=%u stride=%d imageSize=%llu\n", 
           (void*)image, width, height, videoFrame.stride, (unsigned long long)imageSize);
    
    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingMemory);
    
    // Copy frame data to staging buffer
    void* data;
    vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data);
    
    // Copy row by row if stride doesn't match what GPU expects
    int gpuStride = width * bpp;  // What GPU expects (tightly packed)
    
    if (videoFrame.stride == gpuStride) {
        // Simple case: strides match, direct copy
        std::memcpy(data, videoFrame.data, (size_t)imageSize);
        logs("[+] Direct copy (stride matches): %d bytes\n", (int)imageSize);
    } else {
        // Complex case: need to copy row by row, skipping padding
        logs("[+] Row-by-row copy (frame stride=%d, gpu stride=%d)\n", 
               videoFrame.stride, gpuStride);
        
        uint8_t* dst = static_cast<uint8_t*>(data);
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* srcRow = videoFrame.data + (y * videoFrame.stride);
            uint8_t* dstRow = dst + (y * gpuStride);
            std::memcpy(dstRow, srcRow, gpuStride);
        }
    }
    
    vkUnmapMemory(device, stagingMemory);
    logs("[+] staging buffer filled and unmapped\n");
    
    // Transition image layout for transfer
    logs("[+] transitionImageLayout: SHADER_READ -> TRANSFER_DST for image %p\n", (void*)image);
    transitionImageLayout(image, videoTextureFormat,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    logs("[+] transitionImageLayout to TRANSFER_DST complete\n");
    
    // Copy buffer to image
    logs("[+] copyBufferToImage: stagingBuffer=%p -> image=%p\n", (void*)stagingBuffer, (void*)image);
    copyBufferToImage(stagingBuffer, image, width, height);
    logs("[+] copyBufferToImage complete\n");
    
    // Transition back to shader read
    logs("[+] transitionImageLayout: TRANSFER_DST -> SHADER_READ for image %p\n", (void*)image);
    transitionImageLayout(image, videoTextureFormat,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    logs("[+] transitionImageLayout back to SHADER_READ complete\n");
    
    // Cleanup staging resources
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    logs("[+] staging resources destroyed\n");
}

void UI::copyBufferToImage(VkBuffer buffer, VkImage image,
                          uint32_t width, uint32_t height) {
    logs("[+] copyBufferToImage START buffer=%p image=%p size=%ux%u\n", (void*)buffer, (void*)image, width, height);
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    
    vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    logs("[+] vkCmdCopyBufferToImage recorded\n");
    endSingleTimeCommands(commandBuffer);
    logs("[+] copyBufferToImage END\n");
}

void UI::transitionImageLayout(VkImage image, VkFormat format,
                               VkImageLayout oldLayout, VkImageLayout newLayout) {
    logs("[+] transitionImageLayout START image=%p format=%d old=%d new=%d\n", (void*)image, (int)format, (int)oldLayout, (int)newLayout);
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    
    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;
    
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::invalid_argument("Unsupported layout transition");
    }
    
    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);
    logs("[+] vkCmdPipelineBarrier recorded\n");
    endSingleTimeCommands(commandBuffer);
    logs("[+] transitionImageLayout END\n");
}

// ============================================================================
// UI Window Functions
// ============================================================================

void UI::fpsWindow(){
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 padding(12.0f, 12.0f);
    ImVec2 windowPos = ImVec2(io.DisplaySize.x - padding.x, padding.y);
    ImVec2 windowPivot = ImVec2(1.0f, 0.0f);
    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, windowPivot);
    ImGui::SetNextWindowBgAlpha(0.0f);

    float ft = io.DeltaTime * 1000.0f;
    for (size_t i = 1; i < renderSettings.frameTimes.size(); ++i) {
        renderSettings.frameTimes[i - 1] = renderSettings.frameTimes[i];
    }
    renderSettings.frameTimes[renderSettings.frameTimes.size() - 1] = ft;
    renderSettings.frameTimeMin = 9999.0f;
    renderSettings.frameTimeMax = 0.0f;
    for (float v : renderSettings.frameTimes) {
        renderSettings.frameTimeMin = std::min(renderSettings.frameTimeMin, v);
        renderSettings.frameTimeMax = std::max(renderSettings.frameTimeMax, v);
    }
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    // Stats window
    if (ImGui::Begin("Photon Stats", nullptr, windowFlags)) {
        ImGuiIO &io = ImGui::GetIO();
        float fps = io.Framerate;
        float ft_ms = (io.DeltaTime > 0.0f) ? (io.DeltaTime * 1000.0f) : 0.0f;
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Text("Frame time: %.3f ms", ft_ms);
        ImGui::Separator();
        ImGui::Text("Renderer: %s", deviceName[0] ? deviceName : "Unknown");
        ImGui::Text("VendorID: 0x%04X  DeviceID: 0x%04X", vendorID, deviceID);
        const char* typeStr = "Other";
        switch (deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: typeStr = "Integrated GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: typeStr = "Discrete GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: typeStr = "Virtual GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU: typeStr = "CPU"; break;
            default: break;
        }
        ImGui::Text("Device Type: %s", typeStr);
        ImGui::Text("Driver: %u  API: %u.%u.%u",
            driverVersion,
            VK_API_VERSION_MAJOR(apiVersion),
            VK_API_VERSION_MINOR(apiVersion),
            VK_API_VERSION_PATCH(apiVersion));
        ImGui::Separator();
        ImGui::Text("Frametime (last %zu):", renderSettings.frameTimes.size());
        ImGui::PlotLines("##ft", renderSettings.frameTimes.data(), (int)renderSettings.frameTimes.size(), 0,
                         nullptr, renderSettings.frameTimeMin, renderSettings.frameTimeMax,
                         ImVec2(240, 80));
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
}

void UI::customShaderWindow(){
    if (!customShader.texture) { return; }

    ImGui::SetNextWindowSize(ImVec2(customShader.x, customShader.y), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Custom Shader")) {
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        if (contentSize.x <= 1.0f || contentSize.y <= 1.0f) {
            contentSize = ImVec2(customShader.x, customShader.y);
        }

        const float epsilon = 0.5f;
        if (contentSize.x > 1.0f && contentSize.y > 1.0f) {
            if (std::fabs(contentSize.x - customShader.x) > epsilon ||
                std::fabs(contentSize.y - customShader.y) > epsilon) {
                customShader.x = contentSize.x;
                customShader.y = contentSize.y;
                customShader.dirty = true;
            }
        }

        ImVec2 drawSize(customShader.x, customShader.y);
        drawSize.x = std::max(drawSize.x, 1.0f);
        drawSize.y = std::max(drawSize.y, 1.0f);
        ImGui::Image(customShader.texture, drawSize);
    }
    ImGui::End();
}

void UI::showVideoDisplay(){
    if (!videoTexture) { return; }
    if (ImGui::Begin("Custom Image", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImVec2 size = videoTextureSize;
        if (size.x <= 0.0f || size.y <= 0.0f) { size = ImVec2(512.0f, 512.0f); }
        ImVec2 available = ImGui::GetContentRegionAvail();
        ImVec2 drawSize = size;
        if (available.x > 0.0f && available.y > 0.0f) {
            float scaleX = available.x / size.x;
            float scaleY = available.y / size.y;
            float scale = scaleX < scaleY ? scaleX : scaleY;
            if (scale < 1.0f) {
                drawSize.x = size.x * scale;
                drawSize.y = size.y * scale;
            }
        }
        ImGui::Image(videoTexture, drawSize);
    }
    ImGui::End();
}

void UI::networkSamplePlot(){
    ImGui::SetNextWindowSize(ImVec2(460.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Network Samples")) {
        ImGui::End();
        return;
    }

    if (!networkINTF) {
        ImGui::TextUnformatted("Network interface unavailable.");
        ImGui::End();
        return;
    }

    const uint16_t sampleCanId = 0x07FF;

    struct ScrollingBuffer {
        int MaxSize;
        int Offset;
        ImVector<ImVec2> Data;
        ScrollingBuffer(int maxSize = 2400) : MaxSize(maxSize), Offset(0) {
            Data.reserve(MaxSize);
        }
        void AddPoint(float x, float y) {
            if (Data.size() < static_cast<size_t>(MaxSize)) {
                Data.push_back(ImVec2(x, y));
            } else {
                Data[Offset] = ImVec2(x, y);
                Offset = (Offset + 1) % MaxSize;
            }
        }
        void Clear() {
            Data.shrink(0);
            Offset = 0;
        }
    };

    static ScrollingBuffer sampleHistory;
    static uint64_t lastSampleValue = 0;
    static bool haveSample = false;
    static float historySeconds = 10.0f;
    static float accumulatedTime = 0.0f;

    ImGui::Text("CAN 0x%03X", sampleCanId);
    ImGui::SliderFloat("History", &historySeconds, 1.0f, 60.0f, "%.1f s");

    ImGuiIO &io = ImGui::GetIO();
    accumulatedTime += io.DeltaTime;

    uint64_t rawValue = 0;
    if (networkINTF->readSample(sampleCanId, rawValue)) {
        lastSampleValue = rawValue;
        haveSample = true;
    }

    if (haveSample) {
        sampleHistory.AddPoint(accumulatedTime, static_cast<float>(lastSampleValue));
    }

    if (!haveSample || sampleHistory.Data.empty()) {
        ImGui::Text("Waiting for samples...");
        ImGui::End();
        return;
    }

    ImGui::Text("Last value: 0x%016llX (%llu)",
                static_cast<unsigned long long>(lastSampleValue),
                static_cast<unsigned long long>(lastSampleValue));

    float yMin = sampleHistory.Data[0].y;
    float yMax = sampleHistory.Data[0].y;
    for (const ImVec2& point : sampleHistory.Data) {
        yMin = std::min(yMin, point.y);
        yMax = std::max(yMax, point.y);
    }
    if (yMin == yMax) {
        yMax = yMin + 1.0f;
        yMin = yMin - 1.0f;
    } else {
        const float padding = (yMax - yMin) * 0.05f;
        yMin -= padding;
        yMax += padding;
    }

    const float plotStartTime = std::max(accumulatedTime - historySeconds, 0.0f);

    if (ImPlot::BeginPlot("##network_samples", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Time (s)", "Value", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_X1, plotStartTime, accumulatedTime, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, yMin, yMax, ImGuiCond_Always);
        ImPlot::SetNextLineStyle(ImGui::GetStyle().Colors[ImGuiCol_PlotLines], 2.0f);
        ImPlot::PlotLine("Sample", &sampleHistory.Data[0].x, &sampleHistory.Data[0].y,
                         static_cast<int>(sampleHistory.Data.size()), 0, sampleHistory.Offset, sizeof(ImVec2));
        ImPlot::EndPlot();
    }

    ImGui::End();
}

void UI::customBackground(){
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;
    if (displaySize.x > 0.0f && displaySize.y > 0.0f) {
        const float epsilon = 0.5f;
        if (std::fabs(background.x - displaySize.x) > epsilon ||
            std::fabs(background.y - displaySize.y) > epsilon) {
            background.x = displaySize.x;
            background.y = displaySize.y;
            background.dirty = true;
        }
    }

    if (!background.texture) { return; }

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    if (!viewport) { return; }

    ImDrawList *drawList = ImGui::GetBackgroundDrawList(viewport);
    ImVec2 min = viewport->Pos;
    ImVec2 max = ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
    drawList->AddImage(this->background.texture, min, max);
}

void UI::setStyle(){
    ImGuiStyle UIstyle = ImGui::GetStyle();
    // pointer to store style, do not modify directly
    ImGuiStyle &setStyle = ImGui::GetStyle();

    ImVec4* colors = UIstyle.Colors;
    colors[ImGuiCol_WindowBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 0.9f);
    colors[ImGuiCol_ChildBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 0.9f);
    colors[ImGuiCol_PopupBg] =
        ImVec4(0.05f, 0.05f, 0.05f, 0.9f);

    // Borders and separators
    colors[ImGuiCol_Border] =
        ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);

    // Text colors
    colors[ImGuiCol_Text] = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
    colors[ImGuiCol_TextDisabled] =
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

    // Headers and title
    colors[ImGuiCol_Header] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    colors[ImGuiCol_HeaderHovered] =
        ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    colors[ImGuiCol_HeaderActive] =
        ImVec4(0.3f, 0.3f, 0.3f, 1.0f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.7f);

    // Buttons
    colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

    // Sliders, checks, etc.
    colors[ImGuiCol_SliderGrab] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

    colors[ImGuiCol_CheckMark] =
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

    // Frame backgrounds
    colors[ImGuiCol_FrameBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2f, 0.2f, 0.2f, 0.9f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.1f, 0.1f, 0.1f, 0.8f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);

    // Resize grips
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.2f, 0.2f, 0.2f, 0.2f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.5f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.4f, 0.4f, 0.4f, 0.7f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.2f, 0.2f, 0.2f, 0.7f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.8f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

    // Misc
    colors[ImGuiCol_PlotLines] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

    colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 1.0f, 0.9f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

    // Transparency handling
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);

    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    setStyle = UIstyle;
    setStyle.ScaleAllSizes(1.0f);
}