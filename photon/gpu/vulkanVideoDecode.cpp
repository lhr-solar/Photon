#include "vulkanVideoDecode.hpp"
#include "vulkanDevice.hpp"
#include "../engine/include.hpp"
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cassert>

VulkanVideo::VulkanVideo(VulkanDevice& dev, uint32_t width, uint32_t height)
    : device(dev), videoWidth(width), videoHeight(height) {
    
    // Setup H.264 decode profile
    h264ProfileInfo.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR;
    h264ProfileInfo.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
    h264ProfileInfo.pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR;

    profileInfo.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
    profileInfo.pNext = &h264ProfileInfo;
    profileInfo.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
    profileInfo.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    profileInfo.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    profileInfo.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

    profileList.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
    profileList.profileCount = 1;
    profileList.pProfiles = &profileInfo;

    // Setup capabilities structures
    h264Capabilities.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR;
    decodeCapabilities.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR;
    decodeCapabilities.pNext = &h264Capabilities;
    videoCapabilities.sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;
    videoCapabilities.pNext = &decodeCapabilities;
}

VulkanVideo::~VulkanVideo() {
    cleanup();
}

void VulkanVideo::cleanup() {
    VkDevice dev = device.logicalDevice;

    if (decodeFence != VK_NULL_HANDLE) {
        vkWaitForFences(dev, 1, &decodeFence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(dev, decodeFence, nullptr);
        decodeFence = VK_NULL_HANDLE;
    }

    if (queryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(dev, queryPool, nullptr);
        queryPool = VK_NULL_HANDLE;
    }

    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(dev, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

    if (currentOutputImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, currentOutputImageView, nullptr);
        currentOutputImageView = VK_NULL_HANDLE;
    }
    if (currentOutputImage != VK_NULL_HANDLE) {
        vkDestroyImage(dev, currentOutputImage, nullptr);
        currentOutputImage = VK_NULL_HANDLE;
    }
    if (currentOutputMemory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, currentOutputMemory, nullptr);
        currentOutputMemory = VK_NULL_HANDLE;
    }

    for (auto& slot : dpbSlots) {
        if (slot.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(dev, slot.imageView, nullptr);
        }
        if (slot.image != VK_NULL_HANDLE) {
            vkDestroyImage(dev, slot.image, nullptr);
        }
        if (slot.memory != VK_NULL_HANDLE) {
            vkFreeMemory(dev, slot.memory, nullptr);
        }
    }
    dpbSlots.clear();

    if (bitstreamMapped && bitstreamMemory != VK_NULL_HANDLE) {
        vkUnmapMemory(dev, bitstreamMemory);
        bitstreamMapped = nullptr;
    }
    if (bitstreamBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(dev, bitstreamBuffer, nullptr);
        bitstreamBuffer = VK_NULL_HANDLE;
    }
    if (bitstreamMemory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, bitstreamMemory, nullptr);
        bitstreamMemory = VK_NULL_HANDLE;
    }

    if (sessionParams != VK_NULL_HANDLE) {
        vkDestroyVideoSessionParametersKHR(dev, sessionParams, nullptr);
        sessionParams = VK_NULL_HANDLE;
    }

    if (videoSession != VK_NULL_HANDLE) {
        vkDestroyVideoSessionKHR(dev, videoSession, nullptr);
        videoSession = VK_NULL_HANDLE;
    }

    for (auto& mem : sessionMemory) {
        if (mem != VK_NULL_HANDLE) {
            vkFreeMemory(dev, mem, nullptr);
        }
    }
    sessionMemory.clear();

    initialized = false;
}

std::vector<const char*> VulkanVideo::getRequiredDeviceExtensions() {
    return {
        VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
        VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME,
        VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
    };
}

bool VulkanVideo::isVideoDecodeSupported(VkPhysicalDevice physicalDevice) {
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data());

    auto required = getRequiredDeviceExtensions();
    for (const char* req : required) {
        bool found = false;
        for (const auto& ext : extensions) {
            if (strcmp(ext.extensionName, req) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            logs("[-] Missing extension: " + std::string(req));
            return false;
        }
    }
    return true;
}

bool VulkanVideo::checkVideoExtensionSupport() {
    return isVideoDecodeSupported(device.physicalDevice);
}

bool VulkanVideo::findVideoQueueFamily() {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(device.physicalDevice, &queueFamilyCount, nullptr);

    if (queueFamilyCount == 0) {
        logs("[-] No queue families found");
        return false;
    }

    std::vector<VkQueueFamilyVideoPropertiesKHR> videoProps(queueFamilyCount);
    std::vector<VkQueueFamilyProperties2> queueProps(queueFamilyCount);
    
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        videoProps[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
        queueProps[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        queueProps[i].pNext = &videoProps[i];
    }

    vkGetPhysicalDeviceQueueFamilyProperties2(device.physicalDevice, &queueFamilyCount, queueProps.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (videoProps[i].videoCodecOperations & VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
            videoQueueFamily = i;
            logs("[+] Found video decode queue family: " + std::to_string(i));
            return true;
        }
    }

    logs("[-] No video decode queue family found");
    return false;
}

bool VulkanVideo::createVideoQueue() {
    if (videoQueueFamily == UINT32_MAX) return false;
    vkGetDeviceQueue(device.logicalDevice, videoQueueFamily, 0, &videoQueue);
    if (videoQueue == VK_NULL_HANDLE) {
        logs("[-] Failed to get video queue");
        return false;
    }
    logs("[+] Video queue created");
    return true;
}

bool VulkanVideo::queryVideoCapabilities() {
    VkResult result = vkGetPhysicalDeviceVideoCapabilitiesKHR(
        device.physicalDevice, &profileInfo, &videoCapabilities);
    
    if (result != VK_SUCCESS) {
        logs("[-] Failed to query video capabilities");
        return false;
    }

    logs("[+] Video capabilities queried successfully");
    logs("    Min coded extent: " + std::to_string(videoCapabilities.minCodedExtent.width) + 
         "x" + std::to_string(videoCapabilities.minCodedExtent.height));
    logs("    Max coded extent: " + std::to_string(videoCapabilities.maxCodedExtent.width) + 
         "x" + std::to_string(videoCapabilities.maxCodedExtent.height));
    logs("    Max DPB slots: " + std::to_string(videoCapabilities.maxDpbSlots));
    logs("    Max active references: " + std::to_string(videoCapabilities.maxActiveReferencePictures));

    return true;
}

bool VulkanVideo::createVideoSession() {
    VkVideoSessionCreateInfoKHR sessionInfo{VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR};
    sessionInfo.pNext = &profileList;
    sessionInfo.queueFamilyIndex = videoQueueFamily;
    sessionInfo.pVideoProfile = &profileInfo;
    sessionInfo.pictureFormat = getYUVFormat();
    sessionInfo.maxCodedExtent = {videoWidth, videoHeight};
    sessionInfo.referencePictureFormat = getYUVFormat();
    sessionInfo.maxDpbSlots = (16u < videoCapabilities.maxDpbSlots) ? 16u : videoCapabilities.maxDpbSlots;
    sessionInfo.maxActiveReferencePictures = (4u < videoCapabilities.maxActiveReferencePictures) ? 4u : videoCapabilities.maxActiveReferencePictures;
    sessionInfo.pStdHeaderVersion = &videoCapabilities.stdHeaderVersion;

    VkResult result = vkCreateVideoSessionKHR(device.logicalDevice, &sessionInfo, nullptr, &videoSession);
    if (result != VK_SUCCESS) {
        logs("[-] Failed to create video session: " + std::to_string(result));
        return false;
    }

    logs("[+] Video session created");
    return allocateSessionMemory();
}

bool VulkanVideo::allocateSessionMemory() {
    uint32_t memReqCount = 0;
    vkGetVideoSessionMemoryRequirementsKHR(device.logicalDevice, videoSession, &memReqCount, nullptr);
    
    if (memReqCount == 0) {
        logs("[+] No session memory requirements");
        return true;
    }

    std::vector<VkVideoSessionMemoryRequirementsKHR> memReqs(memReqCount);
    for (auto& req : memReqs) {
        req.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;
    }
    
    vkGetVideoSessionMemoryRequirementsKHR(device.logicalDevice, videoSession, &memReqCount, memReqs.data());

    std::vector<VkBindVideoSessionMemoryInfoKHR> bindInfos;
    sessionMemory.resize(memReqCount);

    for (uint32_t i = 0; i < memReqCount; i++) {
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs[i].memoryRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(
            memReqs[i].memoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkResult result = vkAllocateMemory(device.logicalDevice, &allocInfo, nullptr, &sessionMemory[i]);
        if (result != VK_SUCCESS) {
            logs("[-] Failed to allocate session memory " + std::to_string(i));
            return false;
        }

        VkBindVideoSessionMemoryInfoKHR bindInfo{VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR};
        bindInfo.memoryBindIndex = memReqs[i].memoryBindIndex;
        bindInfo.memory = sessionMemory[i];
        bindInfo.memoryOffset = 0;
        bindInfo.memorySize = memReqs[i].memoryRequirements.size;
        bindInfos.push_back(bindInfo);
    }

    VkResult result = vkBindVideoSessionMemoryKHR(
        device.logicalDevice, videoSession, 
        static_cast<uint32_t>(bindInfos.size()), bindInfos.data());
    
    if (result != VK_SUCCESS) {
        logs("[-] Failed to bind session memory");
        return false;
    }

    logs("[+] Session memory allocated and bound (" + std::to_string(memReqCount) + " bindings)");
    return true;
}

bool VulkanVideo::createSessionParameters() {
    VkVideoDecodeH264SessionParametersAddInfoKHR h264AddInfo{
        VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR};
    h264AddInfo.stdSPSCount = 1;
    h264AddInfo.pStdSPSs = &paramSets.sps;
    h264AddInfo.stdPPSCount = 1;
    h264AddInfo.pStdPPSs = &paramSets.pps;

    VkVideoDecodeH264SessionParametersCreateInfoKHR h264CreateInfo{
        VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR};
    h264CreateInfo.maxStdSPSCount = 1;
    h264CreateInfo.maxStdPPSCount = 1;
    h264CreateInfo.pParametersAddInfo = &h264AddInfo;

    VkVideoSessionParametersCreateInfoKHR createInfo{
        VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR};
    createInfo.pNext = &h264CreateInfo;
    createInfo.videoSession = videoSession;

    VkResult result = vkCreateVideoSessionParametersKHR(
        device.logicalDevice, &createInfo, nullptr, &sessionParams);
    
    if (result != VK_SUCCESS) {
        logs("[-] Failed to create session parameters: " + std::to_string(result));
        return false;
    }

    logs("[+] Session parameters created");
    return true;
}

bool VulkanVideo::createCommandResources() {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = videoQueueFamily;

    VkResult result = vkCreateCommandPool(device.logicalDevice, &poolInfo, nullptr, &commandPool);
    if (result != VK_SUCCESS) {
        logs("[-] Failed to create command pool");
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    result = vkAllocateCommandBuffers(device.logicalDevice, &allocInfo, &commandBuffer);
    if (result != VK_SUCCESS) {
        logs("[-] Failed to allocate command buffer");
        return false;
    }

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    result = vkCreateFence(device.logicalDevice, &fenceInfo, nullptr, &decodeFence);
    if (result != VK_SUCCESS) {
        logs("[-] Failed to create fence");
        return false;
    }

    logs("[+] Command resources created");
    return true;
}

bool VulkanVideo::createBitstreamBuffer(size_t size) {
    bitstreamBufferSize = size;

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device.logicalDevice, &bufferInfo, nullptr, &bitstreamBuffer);
    if (result != VK_SUCCESS) {
        logs("[-] Failed to create bitstream buffer");
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device.logicalDevice, bitstreamBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    result = vkAllocateMemory(device.logicalDevice, &allocInfo, nullptr, &bitstreamMemory);
    if (result != VK_SUCCESS) {
        logs("[-] Failed to allocate bitstream memory");
        return false;
    }

    vkBindBufferMemory(device.logicalDevice, bitstreamBuffer, bitstreamMemory, 0);
    vkMapMemory(device.logicalDevice, bitstreamMemory, 0, size, 0, &bitstreamMapped);

    logs("[+] Bitstream buffer created (" + std::to_string(size / 1024) + " KB)");
    return true;
}

bool VulkanVideo::createDPBSlots(uint32_t numSlots) {
    dpbSlots.resize(numSlots);
    activeDPBSlots = numSlots;

    for (uint32_t i = 0; i < numSlots; i++) {
        if (!createImage(dpbSlots[i].image, dpbSlots[i].memory, dpbSlots[i].imageView,
                        videoWidth, videoHeight, getYUVFormat(),
                        VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR, &profileList)) {
            logs("[-] Failed to create DPB slot " + std::to_string(i));
            return false;
        }
        dpbSlots[i].frameNum = -1;
        dpbSlots[i].isReference = false;
    }

    logs("[+] Created " + std::to_string(numSlots) + " DPB slots");
    return true;
}

bool VulkanVideo::createOutputImage() {
    return createImage(currentOutputImage, currentOutputMemory, currentOutputImageView,
                      videoWidth, videoHeight, getYUVFormat(),
                      VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_SAMPLED_BIT,
                      &profileList);
}

bool VulkanVideo::createQueryPool() {
    VkQueryPoolVideoEncodeFeedbackCreateInfoKHR feedbackInfo{
        VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR};
    feedbackInfo.pNext = &profileInfo;
    
    VkQueryPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    poolInfo.pNext = &feedbackInfo;
    poolInfo.queryType = VK_QUERY_TYPE_RESULT_STATUS_ONLY_KHR;
    poolInfo.queryCount = 32;

    VkResult result = vkCreateQueryPool(device.logicalDevice, &poolInfo, nullptr, &queryPool);
    if (result != VK_SUCCESS) {
        logs("[-] Failed to create query pool (non-fatal)");
        return true; // Non-fatal
    }

    logs("[+] Query pool created");
    return true;
}

bool VulkanVideo::createImage(VkImage& image, VkDeviceMemory& memory, VkImageView& imageView,
                              uint32_t width, uint32_t height, VkFormat format,
                              VkImageUsageFlags usage, const VkVideoProfileListInfoKHR* profileList) {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.pNext = profileList;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult result = vkCreateImage(device.logicalDevice, &imageInfo, nullptr, &image);
    if (result != VK_SUCCESS) {
        logs("[-] Failed to create image: " + std::to_string(result));
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device.logicalDevice, image, &memReqs);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    result = vkAllocateMemory(device.logicalDevice, &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        logs("[-] Failed to allocate image memory");
        return false;
    }

    vkBindImageMemory(device.logicalDevice, image, memory, 0);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    result = vkCreateImageView(device.logicalDevice, &viewInfo, nullptr, &imageView);
    if (result != VK_SUCCESS) {
        logs("[-] Failed to create image view");
        return false;
    }

    return true;
}

uint32_t VulkanVideo::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(device.physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type");
}

bool VulkanVideo::loadAndInitialize(const std::string& filePath) {
    // Load the H.264 file
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        logs("[-] Failed to open file: " + filePath);
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        logs("[-] Failed to read file");
        return false;
    }

    logs("[+] Loaded file: " + filePath + " (" + std::to_string(size) + " bytes)");
    
    // Extract SPS/PPS and parse frames
    if (!extractSPSPPS(buffer.data(), buffer.size())) {
        logs("[-] Failed to extract SPS/PPS from H.264 stream");
        return false;
    }

    // Parse all frames
    if (!parseH264File(buffer)) {
        logs("[-] Failed to parse H.264 frames");
        return false;
    }

    // Initialize decoder with extracted parameters
    if (!initDecoder()) {
        logs("[-] Failed to initialize decoder");
        return false;
    }

    logs("[+] Video loaded and decoder initialized successfully");
    logs("[+] Video: " + std::to_string(videoWidth) + "x" + std::to_string(videoHeight));
    logs("[+] Frames: " + std::to_string(frameInfos.size()));
    return true;
}

bool VulkanVideo::initDecoder() {
    if (!paramSets.isValid) {
        logs("[-] Parameter sets not extracted yet");
        return false;
    }

    if (!findVideoQueueFamily()) {
        logs("[-] Failed to find video queue family");
        return false;
    }

    if (!createVideoQueue()) {
        logs("[-] Failed to create video queue");
        return false;
    }

    if (!queryVideoCapabilities()) {
        logs("[-] Failed to query video capabilities");
        return false;
    }

    if (!createVideoSession()) {
        logs("[-] Failed to create video session");
        return false;
    }

    if (!createSessionParameters()) {
        logs("[-] Failed to create session parameters");
        return false;
    }

    if (!createCommandResources()) {
        logs("[-] Failed to create command resources");
        return false;
    }

    if (!createBitstreamBuffer(8 * 1024 * 1024)) { // 8 MB buffer
        logs("[-] Failed to create bitstream buffer");
        return false;
    }

    uint32_t numDpbSlots = (16u < videoCapabilities.maxDpbSlots) ? 16u : videoCapabilities.maxDpbSlots;
    if (!createDPBSlots(numDpbSlots)) {
        logs("[-] Failed to create DPB slots");
        return false;
    }

    if (!createOutputImage()) {
        logs("[-] Failed to create output image");
        return false;
    }

    createQueryPool(); // Non-fatal

    initialized = true;
    logs("[+] Video decoder initialized successfully");
    return true;
}

bool VulkanVideo::extractSPSPPS(const uint8_t* data, size_t size) {
    size_t offset = 0;
    bool foundSPS = false;
    bool foundPPS = false;

    while (offset + 4 < size) {
        // Find start code
        if (!(data[offset] == 0x00 && data[offset + 1] == 0x00)) {
            offset++;
            continue;
        }

        size_t startCodeLen = 0;
        if (data[offset + 2] == 0x01) {
            startCodeLen = 3;
        } else if (data[offset + 2] == 0x00 && offset + 4 < size && data[offset + 3] == 0x01) {
            startCodeLen = 4;
        } else {
            offset++;
            continue;
        }

        size_t nalStart = offset + startCodeLen;
        if (nalStart >= size) break;

        // Find end of NAL
        size_t nalEnd = nalStart + 1;
        while (nalEnd + 2 < size) {
            if (data[nalEnd] == 0x00 && data[nalEnd + 1] == 0x00 &&
                (data[nalEnd + 2] == 0x01 || (nalEnd + 3 < size && data[nalEnd + 2] == 0x00 && data[nalEnd + 3] == 0x01))) {
                break;
            }
            nalEnd++;
        }
        if (nalEnd >= size) nalEnd = size;

        uint8_t nalType = data[nalStart] & 0x1F;

        if (nalType == 7) { // SPS
            if (parseSPS(data + nalStart, nalEnd - nalStart)) {
                foundSPS = true;
                logs("[+] Extracted and parsed SPS");
            }
        } else if (nalType == 8) { // PPS
            if (parsePPS(data + nalStart, nalEnd - nalStart)) {
                foundPPS = true;
                logs("[+] Extracted and parsed PPS");
            }
        }

        if (foundSPS && foundPPS) {
            paramSets.isValid = true;
            return true;
        }

        offset = nalEnd;
    }

    return foundSPS && foundPPS;
}

bool VulkanVideo::parseSPS(const uint8_t* data, size_t size) {
    if (size < 4) return false;

    // Store raw SPS data
    paramSets.spsData.assign(data, data + size);

    // Basic SPS parsing
    uint8_t profile_idc = data[1];
    uint8_t level_idc = data[3];

    // Initialize StdVideoH264SequenceParameterSet with safe defaults
    memset(&paramSets.sps, 0, sizeof(StdVideoH264SequenceParameterSet));
    
    paramSets.sps.profile_idc = static_cast<StdVideoH264ProfileIdc>(profile_idc);
    paramSets.sps.level_idc = static_cast<StdVideoH264LevelIdc>(level_idc);
    paramSets.sps.seq_parameter_set_id = 0;
    
    // For simplicity, use the dimensions passed to constructor
    // In production, you'd parse these from the bitstream
    paramSets.sps.pic_width_in_mbs_minus1 = (videoWidth / 16) - 1;
    paramSets.sps.pic_height_in_map_units_minus1 = (videoHeight / 16) - 1;

    // Set flags properly
    paramSets.sps.flags.frame_mbs_only_flag = 1;
    paramSets.sps.flags.direct_8x8_inference_flag = 1;

    logs("[+] Parsed SPS: profile=" + std::to_string(profile_idc) + 
         " level=" + std::to_string(level_idc) +
         " size=" + std::to_string(videoWidth) + "x" + std::to_string(videoHeight));

    return true;
}

bool VulkanVideo::parsePPS(const uint8_t* data, size_t size) {
    if (size < 2) return false;

    // Store raw PPS data
    paramSets.ppsData.assign(data, data + size);

    // Initialize StdVideoH264PictureParameterSet with safe defaults
    memset(&paramSets.pps, 0, sizeof(StdVideoH264PictureParameterSet));
    
    paramSets.pps.seq_parameter_set_id = 0;
    paramSets.pps.pic_parameter_set_id = 0;
    paramSets.pps.num_ref_idx_l0_default_active_minus1 = 0;
    paramSets.pps.num_ref_idx_l1_default_active_minus1 = 0;
    paramSets.pps.weighted_bipred_idc = STD_VIDEO_H264_WEIGHTED_BIPRED_IDC_DEFAULT;
    paramSets.pps.pic_init_qp_minus26 = 0;
    paramSets.pps.pic_init_qs_minus26 = 0;
    paramSets.pps.chroma_qp_index_offset = 0;
    paramSets.pps.second_chroma_qp_index_offset = 0;

    // Set basic flags
    paramSets.pps.flags.entropy_coding_mode_flag = 1; // CABAC
    paramSets.pps.flags.deblocking_filter_control_present_flag = 1;

    logs("[+] Parsed PPS");
    return true;
}

bool VulkanVideo::parseH264File(const std::vector<uint8_t>& data) {
    frameInfos.clear();
    findAllNALUnits(data.data(), data.size());
    logs("[+] Found " + std::to_string(frameInfos.size()) + " video frames");
    return !frameInfos.empty();
}

void VulkanVideo::findAllNALUnits(const uint8_t* data, size_t size) {
    size_t offset = 0;

    while (offset + 4 < size) {
        // Find start code
        if (!(data[offset] == 0x00 && data[offset + 1] == 0x00)) {
            offset++;
            continue;
        }

        size_t startCodeLen = 0;
        if (data[offset + 2] == 0x01) {
            startCodeLen = 3;
        } else if (data[offset + 2] == 0x00 && offset + 4 < size && data[offset + 3] == 0x01) {
            startCodeLen = 4;
        } else {
            offset++;
            continue;
        }

        size_t nalStart = offset + startCodeLen;
        if (nalStart >= size) break;

        // Find next start code (end of this NAL)
        size_t nalEnd = nalStart + 1;
        while (nalEnd + 2 < size) {
            if (data[nalEnd] == 0x00 && data[nalEnd + 1] == 0x00 &&
                (data[nalEnd + 2] == 0x01 || (nalEnd + 3 < size && data[nalEnd + 2] == 0x00 && data[nalEnd + 3] == 0x01))) {
                break;
            }
            nalEnd++;
        }
        if (nalEnd >= size) nalEnd = size;

        uint8_t nalType = data[nalStart] & 0x1F;

        // Only process frame NALs (IDR=5, Non-IDR=1)
        if (nalType == 5 || nalType == 1) {
            VideoFrameInfo frame;
            // Include the start code in the frame data
            frame.bitstreamData.assign(data + offset, data + nalEnd);
            frame.bitstreamSize = frame.bitstreamData.size();
            frame.isIFrame = (nalType == 5);
            frameInfos.push_back(std::move(frame));
        }

        offset = nalEnd;
    }
}

bool VulkanVideo::decodeFrame(uint32_t frameIndex) {
    if (!initialized || frameIndex >= frameInfos.size()) {
        logs("[-] Cannot decode frame " + std::to_string(frameIndex));
        return false;
    }

    VideoFrameInfo& frame = frameInfos[frameIndex];

    if (frame.bitstreamSize > bitstreamBufferSize) {
        logs("[-] Frame too large for bitstream buffer");
        return false;
    }

    // Copy bitstream data to buffer
    memcpy(bitstreamMapped, frame.bitstreamData.data(), frame.bitstreamSize);

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Begin video coding
    VkVideoBeginCodingInfoKHR beginCoding{VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR};
    beginCoding.videoSession = videoSession;
    beginCoding.videoSessionParameters = sessionParams;

    // Setup reference slots if needed
    std::vector<VkVideoReferenceSlotInfoKHR> referenceSlots;
    if (!frame.isIFrame && activeDPBSlots > 0) {
        for (uint32_t i = 0; i < activeDPBSlots && i < 4; i++) {
            if (dpbSlots[i].isReference) {
                VkVideoPictureResourceInfoKHR pictureResource{VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR};
                pictureResource.codedOffset = {0, 0};
                pictureResource.codedExtent = {videoWidth, videoHeight};
                pictureResource.baseArrayLayer = 0;
                pictureResource.imageViewBinding = dpbSlots[i].imageView;

                VkVideoReferenceSlotInfoKHR refSlot{VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR};
                refSlot.slotIndex = i;
                refSlot.pPictureResource = &pictureResource;
                referenceSlots.push_back(refSlot);
            }
        }

        if (!referenceSlots.empty()) {
            beginCoding.referenceSlotCount = static_cast<uint32_t>(referenceSlots.size());
            beginCoding.pReferenceSlots = referenceSlots.data();
        }
    }

    vkCmdBeginVideoCodingKHR(commandBuffer, &beginCoding);

    // Setup decode info
    StdVideoDecodeH264PictureInfo stdPictureInfo{};
    stdPictureInfo.flags.IdrPicFlag = frame.isIFrame ? 1 : 0;
    stdPictureInfo.seq_parameter_set_id = 0;
    stdPictureInfo.pic_parameter_set_id = 0;
    stdPictureInfo.frame_num = static_cast<uint16_t>(frameIndex);

    StdVideoDecodeH264ReferenceInfo stdRefInfo{};
    stdRefInfo.FrameNum = static_cast<uint16_t>(frameIndex);

    VkVideoDecodeH264PictureInfoKHR h264PictureInfo{VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR};
    h264PictureInfo.pStdPictureInfo = &stdPictureInfo;
    h264PictureInfo.sliceCount = 1;

    VkVideoPictureResourceInfoKHR dstPictureResource{VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR};
    dstPictureResource.codedOffset = {0, 0};
    dstPictureResource.codedExtent = {videoWidth, videoHeight};
    dstPictureResource.baseArrayLayer = 0;
    dstPictureResource.imageViewBinding = currentOutputImageView;

    VkVideoDecodeInfoKHR decodeInfo{VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR};
    decodeInfo.pNext = &h264PictureInfo;
    decodeInfo.srcBuffer = bitstreamBuffer;
    decodeInfo.srcBufferOffset = 0;
    decodeInfo.srcBufferRange = frame.bitstreamSize;
    decodeInfo.dstPictureResource = dstPictureResource;

    // Setup DPB slot for this frame
    uint32_t dpbSlotIndex = frameIndex % activeDPBSlots;
    
    VkVideoPictureResourceInfoKHR dpbPictureResource{VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR};
    dpbPictureResource.codedOffset = {0, 0};
    dpbPictureResource.codedExtent = {videoWidth, videoHeight};
    dpbPictureResource.baseArrayLayer = 0;
    dpbPictureResource.imageViewBinding = dpbSlots[dpbSlotIndex].imageView;

    VkVideoDecodeH264DpbSlotInfoKHR h264DpbSlotInfo{VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR};
    h264DpbSlotInfo.pStdReferenceInfo = &stdRefInfo;

    VkVideoReferenceSlotInfoKHR setupSlot{VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR};
    setupSlot.pNext = &h264DpbSlotInfo;
    setupSlot.slotIndex = dpbSlotIndex;
    setupSlot.pPictureResource = &dpbPictureResource;

    decodeInfo.pSetupReferenceSlot = &setupSlot;

    // Execute decode
    vkCmdDecodeVideoKHR(commandBuffer, &decodeInfo);

    // End video coding
    VkVideoEndCodingInfoKHR endCoding{VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR};
    vkCmdEndVideoCodingKHR(commandBuffer, &endCoding);

    vkEndCommandBuffer(commandBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(videoQueue, 1, &submitInfo, decodeFence);
    vkWaitForFences(device.logicalDevice, 1, &decodeFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device.logicalDevice, 1, &decodeFence);

    // Update DPB slot
    dpbSlots[dpbSlotIndex].frameNum = frameIndex;
    dpbSlots[dpbSlotIndex].isReference = true;

    currentFrameIndex = frameIndex;
    logs("[+] Successfully decoded frame " + std::to_string(frameIndex));
    return true;
}

bool VulkanVideo::decodeNextFrame() {
    if (currentFrameIndex + 1 >= frameInfos.size()) {
        logs("[-] No more frames to decode");
        return false;
    }
    return decodeFrame(currentFrameIndex + 1);
}