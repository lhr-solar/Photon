#pragma once
#include <string>
#include <cstdint>
#include <vulkan/vulkan.h>
#include "videoDecoder.hpp"
#include "frame.hpp"

// Simple flags for configuration
enum Mp4VideoFlags {
    MP4_VIDEO_LOOP = 1 << 0,      // Loop playback
    MP4_VIDEO_AUTOPLAY = 1 << 1,  //play immed
};

class mp4Video {
public:
    mp4Video();
    ~mp4Video();

    // Init video with vulkan context
    bool init(VkDevice device, 
              VkPhysicalDevice physicalDevice,
              VkCommandPool commandPool,
              VkQueue graphicsQueue,
              VkDescriptorPool descriptorPool,
              VkDescriptorSetLayout descriptorSetLayout,
              VkSampler sampler);
    //load
    bool loadVideo(const std::string& filePath, uint32_t flags = 0);

    // playback
    void startPlayback();
    void pausePlayback();
    void stopPlayback();
    bool isPlaying() const { return m_playing; }

    // Returns true if frame was updated
    bool update();

    // Get texture ID for ImGui rendering
    // Usage: ImGui::Image((ImTextureID)videoInstance.texture, size);
    uint64_t texture() const { return (uint64_t)m_descriptorSet; }

    // Video properties
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    double duration() const { return m_duration; }
    double currentTime() const { return m_currentTime; }
    bool isLoaded() const { return m_loaded; }

    // Cleanup
    void cleanup();

private:
    // Vulkan context
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    // Video resources
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;

    // Staging buffer for uploads
    VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
    VkDeviceSize m_stagingSize = 0;

    // Decoder and frame management
    videoDecoder m_decoder;
    frame m_currentFrame;

    // Video state
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    double m_duration = 0.0;
    double m_currentTime = 0.0;
    bool m_loaded = false;
    bool m_playing = false;
    bool m_loop = false;

    // Internal helpers
    bool createTexture(uint32_t width, uint32_t height);
    void destroyTexture();
    bool uploadFrame(const frame& videoFrame);
    void transitionImageLayout(VkImage image, VkImageLayout oldLayout, 
                               VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, 
                          uint32_t width, uint32_t height);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
};