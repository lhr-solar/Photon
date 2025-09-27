#include "VulkanVideo.hpp"
#include "vulkanDevice.hpp"
#include "../engine/include.hpp"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cassert>

VulkanVideo::VulkanVideo(VulkanDevice& dev, uint32_t width, uint32_t height)
    : device(dev), videoWidth(width), videoHeight(height) {
    
    // --- Decode profile setup ---
    decodeH264Profile.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
    decodeH264Profile.pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR;

    decodeProfileInfo.pNext = &decodeH264Profile;
    decodeProfileInfo.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
    decodeProfileInfo.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    decodeProfileInfo.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    decodeProfileInfo.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

    decodeProfileList.profileCount = 1;
    decodeProfileList.pProfiles = &decodeProfileInfo;

    decodeCapabilities.pNext = nullptr;
    videoCapabilities.pNext = &decodeCapabilities;

    // --- Encode profile setup ---
    encodeH264Profile.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
    encodeProfileInfo.pNext = &encodeH264Profile;
    encodeProfileInfo.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR;
    encodeProfileInfo.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    encodeProfileInfo.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    encodeProfileInfo.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

    encodeProfileList.profileCount = 1;
    encodeProfileList.pProfiles = &encodeProfileInfo;
}

VulkanVideo::~VulkanVideo() {
    cleanup();
}

// --------------------- Cleanup ---------------------
void VulkanVideo::cleanup() {
    VkDevice dev = device.logicalDevice;

    if (queryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(dev, queryPool, nullptr);
        queryPool = VK_NULL_HANDLE;
    }

    if (decodeSessionParams != VK_NULL_HANDLE) {
        vkDestroyVideoSessionParametersKHR(dev, decodeSessionParams, nullptr);
        decodeSessionParams = VK_NULL_HANDLE;
    }
    if (encodeSessionParams != VK_NULL_HANDLE) {
        vkDestroyVideoSessionParametersKHR(dev, encodeSessionParams, nullptr);
        encodeSessionParams = VK_NULL_HANDLE;
    }

    if (decodeSession != VK_NULL_HANDLE) {
        vkDestroyVideoSessionKHR(dev, decodeSession, nullptr);
        decodeSession = VK_NULL_HANDLE;
    }
    if (encodeSession != VK_NULL_HANDLE) {
        vkDestroyVideoSessionKHR(dev, encodeSession, nullptr);
        encodeSession = VK_NULL_HANDLE;
    }

    if (decodedImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, decodedImageView, nullptr);
        decodedImageView = VK_NULL_HANDLE;
    }
    if (decodedImage != VK_NULL_HANDLE) {
        vkDestroyImage(dev, decodedImage, nullptr);
        decodedImage = VK_NULL_HANDLE;
    }
    if (decodedImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, decodedImageMemory, nullptr);
        decodedImageMemory = VK_NULL_HANDLE;
    }

    if (decodeFence != VK_NULL_HANDLE) {
        vkDestroyFence(dev, decodeFence, nullptr);
        decodeFence = VK_NULL_HANDLE;
    }
    if (videoCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(dev, videoCommandPool, nullptr);
        videoCommandPool = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < numDPBSlots; i++) {
        if (dpbImageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(dev, dpbImageViews[i], nullptr);
            dpbImageViews[i] = VK_NULL_HANDLE;
        }
        if (dpbImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(dev, dpbImages[i], nullptr);
            dpbImages[i] = VK_NULL_HANDLE;
        }
        if (dpbImageMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(dev, dpbImageMemory[i], nullptr);
            dpbImageMemory[i] = VK_NULL_HANDLE;
        }
    }

    for (auto& mem : sessionMemory) {
        if (mem != VK_NULL_HANDLE) {
            vkFreeMemory(dev, mem, nullptr);
            mem = VK_NULL_HANDLE;
        }
    }
}

// --------------------- Device Extensions ---------------------
std::vector<const char*> VulkanVideo::getRequiredDeviceExtensions() {
    return {
        VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
        VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME,
        VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME,
        VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME,
        VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME
    };
}

VkVideoProfileInfoKHR VulkanVideo::getEncodeProfileInfo() {
    return encodeProfileInfo;
}

// --------------------- Encode ---------------------
bool VulkanVideo::createEncodeVideoSession(uint32_t width, uint32_t height) {
    VkVideoSessionCreateInfoKHR sessionInfo{VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR};
    sessionInfo.pNext = &encodeProfileList;
    sessionInfo.queueFamilyIndex = videoEncodeQueueFamily;
    sessionInfo.pVideoProfile = &encodeProfileInfo;
    sessionInfo.pictureFormat = getYUVFormat();
    sessionInfo.maxCodedExtent = {width, height};
    sessionInfo.referencePictureFormat = getYUVFormat();
    sessionInfo.maxDpbSlots = 4;
    sessionInfo.maxActiveReferencePictures = 1;

    VkResult result = vkCreateVideoSessionKHR(device.logicalDevice, &sessionInfo, nullptr, &encodeSession);
    if (result != VK_SUCCESS) {
        logs("[-] Failed to create encode video session");
        return false;
    }
    logs("[+] Encode video session created");
    return true;
}

bool VulkanVideo::createEncodeVideoSessionParameters(
    const StdVideoH264SequenceParameterSet& sps,
    const StdVideoH264PictureParameterSet& pps) {
    
    encodeSessionParamsAddInfo.stdSPSCount = 1;
    encodeSessionParamsAddInfo.pStdSPSs = &sps;
    encodeSessionParamsAddInfo.stdPPSCount = 1;
    encodeSessionParamsAddInfo.pStdPPSs = &pps;

    encodeSessionParamsCreateInfo.maxStdSPSCount = 1;
    encodeSessionParamsCreateInfo.maxStdPPSCount = 1;
    encodeSessionParamsCreateInfo.pParametersAddInfo = &encodeSessionParamsAddInfo;

    VkVideoSessionParametersCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR};
    createInfo.pNext = &encodeSessionParamsCreateInfo;
    createInfo.videoSession = encodeSession;

    VkResult result = vkCreateVideoSessionParametersKHR(device.logicalDevice, &createInfo, nullptr, &encodeSessionParams);
    if (result != VK_SUCCESS) {
        logs("[-] Failed to create encode session parameters");
        return false;
    }
    logs("[+] Encode session parameters created");
    return true;
}

bool VulkanVideo::encodeFrame(VkCommandBuffer commandBuffer,
                              VkVideoPictureResourceInfoKHR& inputPicture,
                              VkBuffer outputBuffer,
                              VkDeviceSize bufferSize) {
    if (encodeSession == VK_NULL_HANDLE) {
        logs("[-] Encode session not initialized");
        return false;
    }

    VkVideoBeginCodingInfoKHR beginCoding{VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR};
    beginCoding.videoSession = encodeSession;
    beginCoding.videoSessionParameters = encodeSessionParams;

    vkCmdBeginVideoCodingKHR(commandBuffer, &beginCoding);

    VkVideoEncodeInfoKHR encodeInfo{VK_STRUCTURE_TYPE_VIDEO_ENCODE_INFO_KHR};
    encodeInfo.dstBuffer = outputBuffer;
    encodeInfo.dstBufferOffset = 0;
    encodeInfo.dstBufferRange = bufferSize;
    encodeInfo.srcPictureResource = inputPicture;

    vkCmdEncodeVideoKHR(commandBuffer, &encodeInfo);

    VkVideoEndCodingInfoKHR endCoding{VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR};
    vkCmdEndVideoCodingKHR(commandBuffer, &endCoding);

    logs("[+] Encode frame submitted");
    return true;
}

// --------------------- Decode Helpers ---------------------
bool VulkanVideo::isVideoDecodeSupported(VkPhysicalDevice physicalDevice) {
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data());

    const std::vector<const char*> required = getRequiredDeviceExtensions();
    for (auto& req : required) {
        bool found = false;
        for (auto& ext : extensions) {
            if (strcmp(ext.extensionName, req) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool VulkanVideo::checkVideoExtensionSupport() {
    return isVideoDecodeSupported(device.physicalDevice);
}

bool VulkanVideo::findVideoDecodeQueueFamily() {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(device.physicalDevice, &queueFamilyCount, nullptr);

    if (queueFamilyCount == 0) return false;

    std::vector<VkQueueFamilyProperties2> queueProps(queueFamilyCount, {VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2});
    vkGetPhysicalDeviceQueueFamilyProperties2(device.physicalDevice, &queueFamilyCount, queueProps.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        VkQueueFamilyVideoPropertiesKHR videoProps{VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR};
        queueProps[i].pNext = &videoProps;

        if (videoProps.videoCodecOperations & VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
            videoDecodeQueueFamily = i;
            return true;
        }
    }

    logs("[-] No suitable video decode queue family found");
    return false;
}

bool VulkanVideo::createVideoQueue() {
    if (videoDecodeQueueFamily == UINT32_MAX) return false;
    vkGetDeviceQueue(device.logicalDevice, videoDecodeQueueFamily, 0, &videoDecodeQueue);
    return videoDecodeQueue != VK_NULL_HANDLE;
}

bool VulkanVideo::queryVideoCapabilities() {
    VkVideoCapabilitiesKHR caps{VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR};
    caps.pNext = &decodeCapabilities;
    VkResult result = vkGetPhysicalDeviceVideoCapabilitiesKHR(device.physicalDevice, &decodeProfileInfo, &caps);
    if (result != VK_SUCCESS) return false;
    videoCapabilities = caps;
    return true;
}

bool VulkanVideo::createBasicResources() {
    if (!createDPBResources()) return false;
    if (!createBitstreamBuffer(4 * 1024 * 1024)) return false; // 4 MB buffer
    if (!createQueryPool()) return false;
    return true;
}

// --------------------- Decode ---------------------
bool VulkanVideo::initDecoder(const H264ParameterSets& paramSets) {
    parameterSets = paramSets;
    if (!findVideoDecodeQueueFamily()) {
        logs("[-] Failed to find video decode queue family");
        return false;
    }
    if (!createVideoQueue()) {
        logs("[-] Failed to create decode video queue");
        return false;
    }
    if (!queryVideoCapabilities()) {
        logs("[-] Failed to query video capabilities");
        return false;
    }
    if (!createBasicResources()) {
        logs("[-] Failed to create basic decode resources");
        return false;
    }
    if (!createVideoSession()) {
        logs("[-] Failed to create decode video session");
        return false;
    }
    if (!createSessionParameters(paramSets)) {
        logs("[-] Failed to create decode session parameters");
        return false;
    }

    initialized = true;
    logs("[+] Video decoder initialized");
    return true;
}

VkImage VulkanVideo::getCurrentDecodedFrame() const {
    return decodedImage;
}

VkImageView VulkanVideo::getCurrentDecodedImageView() const {
    return decodedImageView;
}

bool VulkanVideo::isLastDecodeSuccessful() const {
    return decodedImage != VK_NULL_HANDLE;
}

bool VulkanVideo::decodeFrame(uint32_t frameIndex) {
    if (!initialized || frameIndex >= frameInfos.size()) return false;

    VideoFrameInfo& frame = frameInfos[frameIndex];

    VkVideoDecodeH264PictureInfoKHR h264PictureInfo{VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR};
    VkVideoPictureResourceInfoKHR pictureInfo{VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR};
    pictureInfo.pNext = &h264PictureInfo;              // Attach H.264 info
    pictureInfo.imageViewBinding = dpbImageViews[currentDPBSlot]; // DPB slot image view
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(decodeCommandBuffer, &beginInfo);

    VkVideoBeginCodingInfoKHR beginCoding{VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR};
    beginCoding.videoSession = decodeSession;
    beginCoding.videoSessionParameters = decodeSessionParams;
    vkCmdBeginVideoCodingKHR(decodeCommandBuffer, &beginCoding);

    VkVideoDecodeInfoKHR decodeInfo{VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR};
    decodeInfo.srcBuffer = bitstreamBuffer;
    decodeInfo.srcBufferOffset = 0;
    decodeInfo.srcBufferRange = frame.bitstreamSize;
    decodeInfo.dstPictureResource = pictureInfo;

    vkCmdDecodeVideoKHR(decodeCommandBuffer, &decodeInfo);
    VkVideoEndCodingInfoKHR endCoding{VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR};
    vkCmdEndVideoCodingKHR(decodeCommandBuffer, &endCoding);

    vkEndCommandBuffer(decodeCommandBuffer);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &decodeCommandBuffer;
    vkQueueSubmit(videoDecodeQueue, 1, &submitInfo, decodeFence);
    vkWaitForFences(device.logicalDevice, 1, &decodeFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device.logicalDevice, 1, &decodeFence);

    logs("[+] Decoded frame " + std::to_string(frameIndex));
    return true;
}



bool VulkanVideo::decodeFirstFrame() {
    return decodeFrame(0);
}

bool VulkanVideo::initBasicDecoder() {
    if (!findVideoDecodeQueueFamily()) return false;
    if (!createVideoQueue()) return false;
    if (!queryVideoCapabilities()) return false;
    if (!createBasicResources()) return false;
    return true;
}

bool VulkanVideo::loadVideo(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return false;

    VideoFrameInfo frame;
    frame.bitstreamData = std::move(buffer);
    frame.bitstreamSize = static_cast<size_t>(size);
    frameInfos.push_back(std::move(frame));
    logs("[+] Loaded video file: " + filePath);
    return true;
}
