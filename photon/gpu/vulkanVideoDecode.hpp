#pragma once
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include "vulkanLoader.h"
// H.264 codec headers
#include <vk_video/vulkan_video_codec_h264std.h>
#include <vk_video/vulkan_video_codec_h264std_decode.h>
#include <vector>
#include <memory>
#include <cstdint>
#include <string>

// Forward declarations
struct VulkanDevice;

// H.264 parameter sets
struct H264ParameterSets {
    std::vector<uint8_t> spsData;
    std::vector<uint8_t> ppsData;
    StdVideoH264SequenceParameterSet sps{};
    StdVideoH264PictureParameterSet pps{};
    bool isValid = false;
};

// Video frame information
struct VideoFrameInfo {
    std::vector<uint8_t> bitstreamData;
    size_t bitstreamSize = 0;
    bool isIFrame = false;
};

// DPB slot management
struct DPBSlot {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    int32_t frameNum = -1;
    bool isReference = false;
};

class VulkanVideo {
public:
    VulkanVideo(VulkanDevice& device, uint32_t width, uint32_t height);
    ~VulkanVideo();

    // Capability queries
    static std::vector<const char*> getRequiredDeviceExtensions();
    static bool isVideoDecodeSupported(VkPhysicalDevice physicalDevice);
    bool checkVideoExtensionSupport();

    // Initialization and loading - just provide the H.264 file!
    bool loadAndInitialize(const std::string& filePath);
    
    // Decoding
    bool decodeFrame(uint32_t frameIndex);
    bool decodeNextFrame();
    
    // Accessors
    const std::vector<VideoFrameInfo>& getFrameInfos() const { return frameInfos; }
    VkImage getCurrentDecodedImage() const { return currentOutputImage; }
    VkImageView getCurrentDecodedImageView() const { return currentOutputImageView; }
    uint32_t getFrameCount() const { return static_cast<uint32_t>(frameInfos.size()); }
    uint32_t getVideoWidth() const { return videoWidth; }
    uint32_t getVideoHeight() const { return videoHeight; }
    uint32_t getCurrentFrameIndex() const { return currentFrameIndex; }
    bool isInitialized() const { return initialized; }

    void cleanup();

private:
    VulkanDevice& device;
    uint32_t videoWidth, videoHeight;
    bool initialized = false;
    uint32_t currentFrameIndex = 0;

    // Queue management
    uint32_t videoQueueFamily = UINT32_MAX;
    VkQueue videoQueue = VK_NULL_HANDLE;

    // Command resources
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence decodeFence = VK_NULL_HANDLE;

    // Video session
    VkVideoSessionKHR videoSession = VK_NULL_HANDLE;
    VkVideoSessionParametersKHR sessionParams = VK_NULL_HANDLE;
    std::vector<VkDeviceMemory> sessionMemory;

    // Profile information
    VkVideoProfileInfoKHR profileInfo{VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR};
    VkVideoProfileListInfoKHR profileList{VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR};
    VkVideoDecodeH264ProfileInfoKHR h264ProfileInfo{VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR};
    VkVideoCapabilitiesKHR videoCapabilities{VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR};
    VkVideoDecodeCapabilitiesKHR decodeCapabilities{VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR};
    VkVideoDecodeH264CapabilitiesKHR h264Capabilities{VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR};

    // Parameter sets
    H264ParameterSets paramSets;

    // DPB (Decoded Picture Buffer)
    static constexpr uint32_t MAX_DPB_SLOTS = 17;
    std::vector<DPBSlot> dpbSlots;
    uint32_t activeDPBSlots = 0;

    // Output image
    VkImage currentOutputImage = VK_NULL_HANDLE;
    VkImageView currentOutputImageView = VK_NULL_HANDLE;
    VkDeviceMemory currentOutputMemory = VK_NULL_HANDLE;

    // Bitstream buffer
    VkBuffer bitstreamBuffer = VK_NULL_HANDLE;
    VkDeviceMemory bitstreamMemory = VK_NULL_HANDLE;
    void* bitstreamMapped = nullptr;
    size_t bitstreamBufferSize = 0;

    // Frame data
    std::vector<VideoFrameInfo> frameInfos;

    // Query pool
    VkQueryPool queryPool = VK_NULL_HANDLE;

    // Helper functions
    bool findVideoQueueFamily();
    bool createVideoQueue();
    bool queryVideoCapabilities();
    bool createVideoSession();
    bool createSessionParameters();
    bool allocateSessionMemory();
    bool createCommandResources();
    bool createBitstreamBuffer(size_t size);
    bool createDPBSlots(uint32_t numSlots);
    bool createOutputImage();
    bool createQueryPool();
    
    bool createImage(VkImage& image, VkDeviceMemory& memory, VkImageView& imageView,
                     uint32_t width, uint32_t height, VkFormat format, 
                     VkImageUsageFlags usage, const VkVideoProfileListInfoKHR* profileList);
    
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkFormat getYUVFormat() const { return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM; }
    
    bool parseH264File(const std::vector<uint8_t>& data);
    bool extractSPSPPS(const uint8_t* data, size_t size);
    bool parseSPS(const uint8_t* data, size_t size);
    bool parsePPS(const uint8_t* data, size_t size);
    void findAllNALUnits(const uint8_t* data, size_t size);
    bool initDecoder();
};