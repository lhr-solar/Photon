/*[λ] the photon parsing interface*/
#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstring>

class Parse {
public:
    Parse() = default;
    ~Parse();

    // Initialize Kalman filter with Vulkan resources
    void initKalmanFilter(VkDevice device, VkPhysicalDevice physicalDevice,
                         VkQueue computeQueue, uint32_t computeQueueFamily);

    // Launch compute thread
    void initThreads();

    // Stop compute thread safely
    void stopThreads();

    // Feed accelerometer measurements
    void updateMeasurements(glm::vec2 measurement);

    // Get filtered state [phi, theta, b1, b2]
    glm::vec4 getState();

    // Get covariance matrix
    glm::mat4 getCovariance();

    // Get sigma points
    std::vector<glm::vec4> getSigmaPoints();

    // Get measurement space sigma points
    std::vector<glm::vec2> getZSigmaPoints();

private:
    // Vulkan handles (from engine)
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    uint32_t computeQueueFamily = 0;

    // Buffers (5 total)
    VkBuffer stateBuffer = VK_NULL_HANDLE;           // Binding 0: state [phi, theta, b1, b2]
    VkBuffer lMatrixBuffer = VK_NULL_HANDLE;         // Binding 1: Cholesky L matrix (4x4)
    VkBuffer sigmaPointsBuffer = VK_NULL_HANDLE;     // Binding 2: 9 sigma points (vec4 each)
    VkBuffer covarianceBuffer = VK_NULL_HANDLE;      // Binding 3: covariance matrix (4x4)
    VkBuffer zSigmaPointsBuffer = VK_NULL_HANDLE;    // Binding 4: 9 measurement sigma points (vec2 each)     

    // GPU memory
    VkDeviceMemory stateMemory = VK_NULL_HANDLE;
    VkDeviceMemory lMatrixMemory = VK_NULL_HANDLE;
    VkDeviceMemory sigmaPointsMemory = VK_NULL_HANDLE;
    VkDeviceMemory covarianceMemory = VK_NULL_HANDLE;
    VkDeviceMemory zSigmaPointsMemory = VK_NULL_HANDLE;

    // Staging buffers for CPU-GPU transfer
    VkBuffer stagingStateBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingStateMemory = VK_NULL_HANDLE;
    VkBuffer stagingSigmaBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingSigmaMemory = VK_NULL_HANDLE;
    VkBuffer stagingCovBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingCovMemory = VK_NULL_HANDLE;

    // Descriptor set & pipeline
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;

    // Command pool & buffer
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    // Synchronization
    VkFence computeFence = VK_NULL_HANDLE;

    // Thread management
    std::thread computeThread;
    std::atomic<bool> threadRunning = false;
    std::mutex measurementMutex;
    std::mutex stateMutex;
    std::condition_variable measurementCV;
    glm::vec2 currentMeasurement = glm::vec2(0.0f);
    bool newMeasurementAvailable = false;

    // Cached results
    glm::vec4 cachedState = glm::vec4(1.0f, 1.0f, 1.5f, 2.0f);
    glm::mat4 cachedCovariance = glm::mat4(0.0f);
    std::vector<glm::vec4> cachedSigmaPoints;
    std::vector<glm::vec2> cachedZSigmaPoints;

    // Internal helper functions
    void createBuffers();
    void createStagingBuffers();
    void createDescriptorSet();
    void createComputePipeline();
    void createCommandPool();
    void recordComputeCommand();
    void submitAndWait();
    void readResultsFromGPU();
    void computeLoop();

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
    void copyBufferToStaging(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
};
