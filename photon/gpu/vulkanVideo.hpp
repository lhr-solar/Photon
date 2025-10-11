#pragma once

// Enable beta extensions for Vulkan Video
//add a commen tos I can push hcanges holy
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_video.hpp>

// H.264 codec headers
#include <vk_video/vulkan_video_codec_h264std.h>
#include <vk_video/vulkan_video_codec_h264std_decode.h>
#include <vk_video/vulkan_video_codec_h264std_encode.h>

#include <vector>
#include <memory>
#include <cstdint>
#include <string>
#include <queue>

// Forward declarations
struct VulkanDevice;
class VulkanBuffer;

// Video frame information structure
struct VideoFrameInfo {
    // Vulkan image resources
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    // CPU-side compressed video data for this frame
    std::vector<uint8_t> bitstreamData;  // H.264 / H.265 frame NAL units
    size_t bitstreamSize = 0;            // actual number of bytes in bitstreamData
};

// H.264 parameter sets
struct H264ParameterSets {
    std::vector<uint8_t> spsData;
    std::vector<uint8_t> ppsData;
    StdVideoH264SequenceParameterSet sps{};
    StdVideoH264PictureParameterSet pps{};
    StdVideoH264SequenceParameterSetVui vui{};
    StdVideoH264HrdParameters hrd{};
    StdVideoH264ScalingLists scalingLists{};
    bool isValid = false;
};

// DPB (Decoded Picture Buffer) slot management
struct DPBSlot {
    uint32_t slotIndex = 0;
    int32_t frameNum = 0;
    int32_t picOrderCnt[2] = {0, 0};  // For progressive frames, both values are the same
    bool isActive = false;
    bool isReference = false;
};

// Vulkan Video Decoder/Encoder class
class VulkanVideo {
public:
    VulkanVideo(VulkanDevice& device, uint32_t width, uint32_t height);
    ~VulkanVideo();

    // ---- Capability queries ----
    static std::vector<const char*> getRequiredDeviceExtensions();
    static bool isVideoDecodeSupported(VkPhysicalDevice physicalDevice);
    bool checkVideoExtensionSupport();

    // ---- Decode path ----
    bool initDecoder(const H264ParameterSets& paramSets);
    bool decodeFrame(uint32_t frameIndex);
    bool decodeFirstFrame(); // Simplified method for testing I-frame only
    bool loadVideo(const std::string& filePath);
    bool initBasicDecoder();

    VkImage getCurrentDecodedFrame() const;
    VkImageView getCurrentDecodedImageView() const;
    bool isLastDecodeSuccessful() const;
    uint32_t getFrameCount() const { return static_cast<uint32_t>(frameInfos.size()); }
    uint32_t getVideoWidth() const { return videoWidth; }
    uint32_t getVideoHeight() const { return videoHeight; }

    // ---- Encode path ----
    bool initEncoder(const H264ParameterSets& paramSets);
    bool encodeFrame(VkCommandBuffer commandBuffer,
                     VkVideoPictureResourceInfoKHR& inputPicture,
                     VkBuffer outputBuffer,
                     VkDeviceSize bufferSize);
    bool isVideoHeadersAvailable() const;

    // ---- General ----
    bool isInitialized() const { return initialized; }
    void cleanup();

private:
    // Core references
    VulkanDevice& device;
    uint32_t videoWidth, videoHeight;
    bool initialized = false;

    // Queue families and queues
    uint32_t videoDecodeQueueFamily = UINT32_MAX;
    uint32_t videoEncodeQueueFamily = UINT32_MAX;
    VkQueue videoDecodeQueue = VK_NULL_HANDLE;
    VkQueue videoEncodeQueue = VK_NULL_HANDLE;

    // Decode resources
    VkImage decodedImage = VK_NULL_HANDLE;
    VkImageView decodedImageView = VK_NULL_HANDLE;
    VkDeviceMemory decodedImageMemory = VK_NULL_HANDLE;
    VkCommandPool videoCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer decodeCommandBuffer = VK_NULL_HANDLE;
    VkFence decodeFence = VK_NULL_HANDLE;

    // Video frame data
    std::vector<VideoFrameInfo> frameInfos;

    // Video session objects
    VkVideoSessionKHR decodeSession = VK_NULL_HANDLE;
    VkVideoSessionKHR encodeSession = VK_NULL_HANDLE;
    VkVideoSessionParametersKHR decodeSessionParams = VK_NULL_HANDLE;
    VkVideoSessionParametersKHR encodeSessionParams = VK_NULL_HANDLE;
    std::vector<VkVideoSessionMemoryRequirementsKHR> memoryRequirements;
    std::vector<VkDeviceMemory> sessionMemory;

    // ---- Decode profile info ----
    VkVideoProfileInfoKHR decodeProfileInfo{VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR};
    VkVideoProfileListInfoKHR decodeProfileList{VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR};
    VkVideoDecodeH264ProfileInfoKHR decodeH264Profile{VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR};
    H264ParameterSets parameterSets;

    // ---- Encode profile info ----
    VkVideoProfileInfoKHR encodeProfileInfo{VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR};
    VkVideoProfileListInfoKHR encodeProfileList{VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR};
    VkVideoEncodeH264ProfileInfoKHR encodeH264Profile{VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR};
    VkVideoEncodeH264CapabilitiesKHR encodeCapabilities{VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_CAPABILITIES_KHR};
    VkVideoEncodeH264SessionParametersCreateInfoKHR encodeSessionParamsCreateInfo{
        VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR};
    VkVideoEncodeH264SessionParametersAddInfoKHR encodeSessionParamsAddInfo{
        VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR};

    // ---- DPB resources ----
    static constexpr uint32_t MAX_DPB_SLOTS = 17;
    VkImage dpbImages[MAX_DPB_SLOTS] = {VK_NULL_HANDLE};
    VkImageView dpbImageViews[MAX_DPB_SLOTS] = {VK_NULL_HANDLE};
    VkDeviceMemory dpbImageMemory[MAX_DPB_SLOTS] = {VK_NULL_HANDLE};
    uint32_t numDPBSlots = 0;
    uint32_t currentDPBSlot = 0;
    std::queue<uint32_t> referenceSlots;
    std::vector<DPBSlot> dpbSlots;

    // ---- Capabilities & buffers ----
    VkVideoCapabilitiesKHR videoCapabilities{VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR};
    VkVideoDecodeCapabilitiesKHR decodeCapabilities{VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR};
    VkBuffer bitstreamBuffer = VK_NULL_HANDLE;
    VkDeviceMemory bitstreamBufferMemory = VK_NULL_HANDLE;
    void* bitstreamBufferMapped = nullptr;
    size_t bitstreamBufferSize = 0;
    VkQueryPool queryPool = VK_NULL_HANDLE;

    // ---- Internal helpers ----
    bool findVideoDecodeQueueFamily();
    bool createVideoQueue();
    bool queryVideoCapabilities();
    bool createBasicResources();

    bool findVideoEncodeQueueFamily();
    VkVideoProfileInfoKHR getEncodeProfileInfo();
    bool createEncodeVideoSession(uint32_t width, uint32_t height);
    bool createEncodeVideoSessionParameters(const StdVideoH264SequenceParameterSet& sps,
                                            const StdVideoH264PictureParameterSet& pps);
    bool createVideoSession();
    bool createSessionParameters(const H264ParameterSets& paramSets);
    bool createDPBResources();
    bool createBitstreamBuffer(size_t size);
    bool createQueryPool();

    bool createImage(VkImage& image, VkDeviceMemory& memory, VkImageView& imageView,
                     uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage);
    uint32_t getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;
    VkFormat getYUVFormat() const { return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM; }
    VkFormat getRGBFormat() const { return VK_FORMAT_R8G8B8A8_UNORM; }
};
