#include "mp4Video.hpp"
#include "../engine/include.hpp"
#include <stdexcept>
#include <cstring>

mp4Video::mp4Video() {}
//cleanup on destruction
mp4Video::~mp4Video() {
    cleanup();
}

//initialization function (call once)
bool mp4Video::init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue, 
                                    VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout, VkSampler sampler) {
    //validate handles before rest of initialization
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE) {
        logs("[!] ERROR: mp4Video Initialization Failed Invalid Vulkan handles");
        return false;
    }
    //initialize parameters
    m_device = device;
    m_physicalDevice = physicalDevice;
    m_commandPool = commandPool;
    m_graphicsQueue = graphicsQueue;
    m_descriptorPool = descriptorPool;
    m_descriptorSetLayout = descriptorSetLayout;
    m_sampler = sampler;
    //great success
    logs("[+] mp4Video initialized with Vulkan context");
    return true;
}

// Load video file
bool mp4Video::loadVideo(const std::string& filePath, uint32_t flags) {
    //check initialized
    if (m_device == VK_NULL_HANDLE) {
        logs("[!] ERROR: mp4Video Not initialized. Call init() first.");
        return false;
    }
    cleanup();
    // Open video file
    if (!m_decoder.open(filePath)) {
        logs("[!] ERROR: Failed to open " << filePath);
        return false;
    }
    //set param
    m_width = m_decoder.width();
    m_height = m_decoder.height();
    m_duration = m_decoder.duration();
    m_loop = (flags & MP4_VIDEO_LOOP) != 0;
    // Decode first frame
    if (!m_decoder.decodeNextFrame(m_currentFrame)) {
        logs("[!] ERROR: Failed to decode first frame");
        m_decoder.close();
        return false;
    }
    m_currentTime = m_currentFrame.timestamp;
    // Create Vulkan texture
    if (!createTexture(m_width, m_height)) {
        logs("[!] ERROR: Failed to create texture");
        m_decoder.close();
        m_currentFrame.free();
        return false;
    }
    // Upload first frame
    if (!uploadFrame(m_currentFrame)) {
        logs("[!] ERROR: Failed to upload first frame");
        cleanup();
        return false;
    }
    m_loaded = true;
    if (flags & MP4_VIDEO_AUTOPLAY) {
        startPlayback();
    }
    logs("[+] mp4Video loaded: " << filePath << " (" << m_width << "x" << m_height << ", " << m_duration << "s)");
    return true;
}

//playback controls
void mp4Video::startPlayback() {
    if (!m_loaded) return;
    m_playing = true;
    logs("[+] mp4Video playback started");
}

void mp4Video::pausePlayback() {
    m_playing = false;
    logs("[+] mp4Video playback paused");
}

void mp4Video::stopPlayback() {
    m_playing = false;
    m_currentTime = 0.0;
    // TODO: seek to beginning when seeking implemented
    logs("[+] mp4Video playback stopped");
}

//update (frame refresh)
bool mp4Video::update() {

    if (!m_loaded || !m_playing) {
        logs("[!] ERROR: Video not loaded or not playing");
        return false;
    }
    // Decode next frame
    if (!m_decoder.decodeNextFrame(m_currentFrame)) {
        if (m_loop) {
            // Restart video
            logs("[+] mp4Video: Looping back to start");
            m_decoder.close();
            //TODO: store file path, reopen here 
            m_playing = false;
            return false;
        } else {
            logs("[+] mp4Video: End of video");
            m_playing = false;
            return false;
        }
    }
    m_currentTime = m_currentFrame.timestamp;
    // Upload new frame to GPU
    if (!uploadFrame(m_currentFrame)) {
        logs("[!] ERROR: Failed to upload frame");
        return false;
    }
    return true;
}

void mp4Video::cleanup() {
    m_playing = false;
    m_loaded = false;

    // Cleanup staging buffer
    if (m_stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_stagingBuffer, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
    }
    if (m_stagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_stagingMemory, nullptr);
        m_stagingMemory = VK_NULL_HANDLE;
    }
    m_stagingSize = 0;

    // Cleanup texture
    destroyTexture();

    // Cleanup decoder
    m_decoder.close();
    m_currentFrame.free();

    m_width = 0;
    m_height = 0;
    m_duration = 0.0;
    m_currentTime = 0.0;
}

//texture control functions below
bool mp4Video::createTexture(uint32_t width, uint32_t height) {

    destroyTexture();
    // Choose format
    VkFormat candidates[] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM };
    m_format = VK_FORMAT_UNDEFINED;

    for (VkFormat fmt : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, fmt, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) &&
            (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
            m_format = fmt;
            break;
        }
    }
    if (m_format == VK_FORMAT_UNDEFINED) {
        logs("[!] ERROR: No suitable texture format found");
        return false;
    }

    // Create image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_image) != VK_SUCCESS) {
        logs("[!] ERROR: Failed to create image");
        return false;
    }

    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_device, m_image, &memReqs);
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS) {
        vkDestroyImage(m_device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
        logs("[!] ERROR: Failed to allocate image memory");
        return false;
    }
    vkBindImageMemory(m_device, m_image, m_memory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        vkFreeMemory(m_device, m_memory, nullptr);
        vkDestroyImage(m_device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
        m_memory = VK_NULL_HANDLE;
        logs("[!] ERROR: Failed to create image view");
        return false;
    }

    // Create descriptor set
    VkDescriptorSetAllocateInfo descAllocInfo = {};
    descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descAllocInfo.descriptorPool = m_descriptorPool;
    descAllocInfo.descriptorSetCount = 1;
    descAllocInfo.pSetLayouts = &m_descriptorSetLayout;
    if (vkAllocateDescriptorSets(m_device, &descAllocInfo, &m_descriptorSet) != VK_SUCCESS) {
        destroyTexture();
        logs("[!] ERROR: Failed to allocate descriptor set");
        return false;
    }
    VkDescriptorImageInfo imageDescInfo = {};
    imageDescInfo.sampler = m_sampler;
    imageDescInfo.imageView = m_imageView;
    imageDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet writeDesc = {};
    writeDesc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDesc.dstSet = m_descriptorSet;
    writeDesc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDesc.dstBinding = 0;
    writeDesc.descriptorCount = 1;
    writeDesc.pImageInfo = &imageDescInfo;
    vkUpdateDescriptorSets(m_device, 1, &writeDesc, 0, nullptr);

    // Transition to shader read optimal
    transitionImageLayout(m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    logs("[+] mp4Video: Created texture " << width << "x" << height);
    return true;
}

void mp4Video::destroyTexture() {
    if (m_descriptorSet != VK_NULL_HANDLE && m_descriptorPool != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &m_descriptorSet);
        m_descriptorSet = VK_NULL_HANDLE;
    }
    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

//frame refresh function
bool mp4Video::uploadFrame(const frame& videoFrame) {

    if (!videoFrame.isValid()) {
        logs("[!] ERROR: uploadFrame failed due to Invalid frame");
        return false;
    }
    VkDeviceSize imageSize = (VkDeviceSize)videoFrame.stride * videoFrame.height;

    // Recreate staging buffer if needed
    if (m_stagingBuffer == VK_NULL_HANDLE || imageSize > m_stagingSize) {
        if (m_stagingBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_stagingBuffer, nullptr);
        }
        if (m_stagingMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_stagingMemory, nullptr);
        }
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = imageSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_stagingBuffer) != VK_SUCCESS) {
            logs("[!] ERROR: Failed to create staging buffer");
            return false;
        }
        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(m_device, m_stagingBuffer, &memReqs);
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_stagingMemory) != VK_SUCCESS) {
            vkDestroyBuffer(m_device, m_stagingBuffer, nullptr);
            m_stagingBuffer = VK_NULL_HANDLE;
            logs("[!] ERROR: Failed to allocate staging memory");
            return false;
        }
        vkBindBufferMemory(m_device, m_stagingBuffer, m_stagingMemory, 0);
        m_stagingSize = imageSize;
    }

    // Copy frame data to staging buffer
    void* data;
    vkMapMemory(m_device, m_stagingMemory, 0, imageSize, 0, &data);
    int gpuStride = m_width * 4;
    if (videoFrame.stride == gpuStride) {
        std::memcpy(data, videoFrame.data, imageSize);
    } else {
        uint8_t* dst = static_cast<uint8_t*>(data);
        for (uint32_t y = 0; y < m_height; ++y) {
            std::memcpy(dst + y * gpuStride, videoFrame.data + y * videoFrame.stride, gpuStride);
        }
    }
    
    vkUnmapMemory(m_device, m_stagingMemory);

    // Transition, copy, transition back
    transitionImageLayout(m_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(m_stagingBuffer, m_image, m_width, m_height);
    transitionImageLayout(m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

void mp4Video::transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {

    VkCommandBuffer cmd = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage, dstStage;
    bool b1 = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) && (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    bool b2 = (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) && (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    bool b3 = (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) && (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (b1) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (b2) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (b3) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        logs("[!] ERROR: Unsupported layout transition");
        throw std::runtime_error("Unsupported layout transition");
    }
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    endSingleTimeCommands(cmd);
}

// Copy buffer to image
void mp4Video::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {

    VkCommandBuffer cmd = beginSingleTimeCommands();
    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    endSingleTimeCommands(cmd);
}

uint32_t mp4Video::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    logs("[!] ERROR: Failed to find suitable memory type");
    throw std::runtime_error("Failed to find suitable memory type");
}

VkCommandBuffer mp4Video::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

void mp4Video::endSingleTimeCommands(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}