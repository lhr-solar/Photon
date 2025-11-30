/*[λ] the photon parsing interface*/
#include "parse.hpp"
#include "kalman_filter_comp_spv.hpp"
#include <iostream>
#include <chrono>

Parse::~Parse() {
    stopThreads();

    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

    // Destroy pipeline resources
    if (computePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, computePipeline, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);

    // Destroy command resources
    if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
    if (computeFence != VK_NULL_HANDLE) vkDestroyFence(device, computeFence, nullptr);

    // Destroy buffers and memory
    if (stateBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, stateBuffer, nullptr);
    if (lMatrixBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, lMatrixBuffer, nullptr);
    if (sigmaPointsBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, sigmaPointsBuffer, nullptr);
    if (covarianceBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, covarianceBuffer, nullptr);
    if (zSigmaPointsBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, zSigmaPointsBuffer, nullptr);

    if (stateMemory != VK_NULL_HANDLE) vkFreeMemory(device, stateMemory, nullptr);
    if (lMatrixMemory != VK_NULL_HANDLE) vkFreeMemory(device, lMatrixMemory, nullptr);
    if (sigmaPointsMemory != VK_NULL_HANDLE) vkFreeMemory(device, sigmaPointsMemory, nullptr);
    if (covarianceMemory != VK_NULL_HANDLE) vkFreeMemory(device, covarianceMemory, nullptr);
    if (zSigmaPointsMemory != VK_NULL_HANDLE) vkFreeMemory(device, zSigmaPointsMemory, nullptr);

    // Destroy staging buffers
    if (stagingStateBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, stagingStateBuffer, nullptr);
    if (stagingStateMemory != VK_NULL_HANDLE) vkFreeMemory(device, stagingStateMemory, nullptr);
    if (stagingSigmaBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, stagingSigmaBuffer, nullptr);
    if (stagingSigmaMemory != VK_NULL_HANDLE) vkFreeMemory(device, stagingSigmaMemory, nullptr);
    if (stagingCovBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, stagingCovBuffer, nullptr);
    if (stagingCovMemory != VK_NULL_HANDLE) vkFreeMemory(device, stagingCovMemory, nullptr);
}

void Parse::initKalmanFilter(VkDevice dev, VkPhysicalDevice physDev,
                            VkQueue compQueue, uint32_t compQueueFamily) {
    std::cout << "[+] Initializing Kalman Filter GPU compute...\n";

    device = dev;
    physicalDevice = physDev;
    computeQueue = compQueue;
    computeQueueFamily = compQueueFamily;

    createCommandPool();
    createBuffers();
    createStagingBuffers();
    createDescriptorSet();
    createComputePipeline();

    std::cout << "[+] Kalman Filter GPU compute fully initialized\n";
}

void Parse::initThreads() {
    std::cout << "[+] Kalman compute thread starting...\n";
    threadRunning = true;
    computeThread = std::thread(&Parse::computeLoop, this);
}

void Parse::stopThreads() {
    if (threadRunning) {
        threadRunning = false;
        measurementCV.notify_one();
        if (computeThread.joinable()) computeThread.join();
    }
}

void Parse::createCommandPool() {
    std::cout << "[DEBUG] Creating command pool...\n";
    std::cout.flush();
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = computeQueueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        std::cerr << "[ERROR] Failed to create command pool\n";
        return;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        std::cerr << "[ERROR] Failed to allocate command buffer\n";
    }
}

void Parse::createBuffers() {
    std::cout << "[DEBUG] Creating GPU buffers...\n";

    // State buffer: 4 floats (phi, theta, b1, b2)
    createBuffer(4 * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, stateBuffer, stateMemory);

    // L matrix buffer: 16 floats (4x4)
    createBuffer(16 * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, lMatrixBuffer, lMatrixMemory);

    // Sigma points: 9 vec4 = 36 floats
    createBuffer(9 * 4 * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sigmaPointsBuffer, sigmaPointsMemory);

    // Covariance: 16 floats (4x4)
    createBuffer(16 * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, covarianceBuffer, covarianceMemory);

    // Z-sigma points: 9 vec2 = 18 floats
    createBuffer(9 * 2 * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, zSigmaPointsBuffer, zSigmaPointsMemory);

    // Initialize state buffer with initial values
    VkBuffer stagingInitBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingInitMemory = VK_NULL_HANDLE;
    createBuffer(4 * sizeof(float), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingInitBuffer, stagingInitMemory);

    float* initPtr = nullptr;
    vkMapMemory(device, stagingInitMemory, 0, 4 * sizeof(float), 0, (void**)&initPtr);
    initPtr[0] = 1.0f;   // phi
    initPtr[1] = 1.0f;   // theta
    initPtr[2] = 1.5f;   // b1
    initPtr[3] = 2.0f;   // b2
    vkUnmapMemory(device, stagingInitMemory);

    copyBufferToStaging(stagingInitBuffer, stateBuffer, 4 * sizeof(float));

    vkDestroyBuffer(device, stagingInitBuffer, nullptr);
    vkFreeMemory(device, stagingInitMemory, nullptr);
}

void Parse::createStagingBuffers() {
    std::cout << "[DEBUG] Creating staging buffers...\n";

    createBuffer(4 * sizeof(float), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingStateBuffer, stagingStateMemory);

    createBuffer(9 * 4 * sizeof(float), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingSigmaBuffer, stagingSigmaMemory);

    createBuffer(16 * sizeof(float), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingCovBuffer, stagingCovMemory);
}

void Parse::createDescriptorSet() {
    std::cout << "[DEBUG] Creating descriptor set...\n";

    VkDescriptorSetLayoutBinding bindings[5] = {};
    for (int i = 0; i < 5; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 5;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "[ERROR] Failed to create descriptor set layout\n";
        return;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 5;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        std::cerr << "[ERROR] Failed to create descriptor pool\n";
        return;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        std::cerr << "[ERROR] Failed to allocate descriptor set\n";
        return;
    }

    // Write descriptor bindings
    VkDescriptorBufferInfo bufferInfos[5] = {
        {stateBuffer, 0, 4 * sizeof(float)},
        {lMatrixBuffer, 0, 16 * sizeof(float)},
        {sigmaPointsBuffer, 0, 9 * 4 * sizeof(float)},
        {covarianceBuffer, 0, 16 * sizeof(float)},
        {zSigmaPointsBuffer, 0, 9 * 2 * sizeof(float)}
    };

    VkWriteDescriptorSet writes[5] = {};
    for (int i = 0; i < 5; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &bufferInfos[i];
    }

    vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
}

void Parse::createComputePipeline() {
    std::cout << "[DEBUG] Creating compute pipeline...\n";
    std::cout.flush();

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = kalman_filter_comp_spv_size * sizeof(uint32_t);
    moduleInfo.pCode = kalman_filter_comp_spv;

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        std::cerr << "[ERROR] Failed to create shader module\n";
        return;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        std::cerr << "[ERROR] Failed to create pipeline layout\n";
        return;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.stage = stageInfo;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS) {
        std::cerr << "[ERROR] Failed to create compute pipeline\n";
        return;
    }

    vkDestroyShaderModule(device, shaderModule, nullptr);
    std::cout << "[+] Compute pipeline created with Kalman filter shader\n";
}

void Parse::recordComputeCommand() {
    if (computePipeline == VK_NULL_HANDLE) {
        std::cerr << "[ERROR] Compute pipeline not initialized\n";
        return;
    }

    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    // Dispatch compute shader with 16 threads (matching local_size_x = 16)
    vkCmdDispatch(commandBuffer, 1, 1, 1);

    vkEndCommandBuffer(commandBuffer);
}

void Parse::submitAndWait() {
    if (computeFence == VK_NULL_HANDLE) {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkCreateFence(device, &fenceInfo, nullptr, &computeFence);
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(computeQueue, 1, &submitInfo, computeFence);
    vkWaitForFences(device, 1, &computeFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &computeFence);
}

void Parse::readResultsFromGPU() {
    std::cout << "[DEBUG] Reading results from GPU...\n";

    // Read state
    copyBufferToStaging(stateBuffer, stagingStateBuffer, 4 * sizeof(float));
    float* statePtr = nullptr;
    vkMapMemory(device, stagingStateMemory, 0, 4 * sizeof(float), 0, (void**)&statePtr);
    cachedState = glm::vec4(statePtr[0], statePtr[1], statePtr[2], statePtr[3]);
    vkUnmapMemory(device, stagingStateMemory);

    // Read sigma points
    copyBufferToStaging(sigmaPointsBuffer, stagingSigmaBuffer, 9 * 4 * sizeof(float));
    float* sigmaPtr = nullptr;
    vkMapMemory(device, stagingSigmaMemory, 0, 9 * 4 * sizeof(float), 0, (void**)&sigmaPtr);
    cachedSigmaPoints.resize(9);
    for (int i = 0; i < 9; ++i) {
        cachedSigmaPoints[i] = glm::vec4(sigmaPtr[i*4], sigmaPtr[i*4+1], sigmaPtr[i*4+2], sigmaPtr[i*4+3]);
    }
    vkUnmapMemory(device, stagingSigmaMemory);

    // Read covariance
    copyBufferToStaging(covarianceBuffer, stagingCovBuffer, 16 * sizeof(float));
    float* covPtr = nullptr;
    vkMapMemory(device, stagingCovMemory, 0, 16 * sizeof(float), 0, (void**)&covPtr);
    cachedCovariance = glm::make_mat4(covPtr);
    vkUnmapMemory(device, stagingCovMemory);

    // Read Z-sigma points
    VkBuffer stagingZSigmaBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingZSigmaMemory = VK_NULL_HANDLE;
    createBuffer(9 * 2 * sizeof(float), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingZSigmaBuffer, stagingZSigmaMemory);

    copyBufferToStaging(zSigmaPointsBuffer, stagingZSigmaBuffer, 9 * 2 * sizeof(float));
    float* zSigmaPtr = nullptr;
    vkMapMemory(device, stagingZSigmaMemory, 0, 9 * 2 * sizeof(float), 0, (void**)&zSigmaPtr);
    cachedZSigmaPoints.resize(9);
    for (int i = 0; i < 9; ++i) {
        cachedZSigmaPoints[i] = glm::vec2(zSigmaPtr[i*2], zSigmaPtr[i*2+1]);
    }
    vkUnmapMemory(device, stagingZSigmaMemory);

    vkDestroyBuffer(device, stagingZSigmaBuffer, nullptr);
    vkFreeMemory(device, stagingZSigmaMemory, nullptr);

    // Print results
    std::cout << "\n========== KALMAN FILTER OUTPUT ==========\n";
    std::cout << "[MEASUREMENT] Input: (" << currentMeasurement.x << ", " << currentMeasurement.y << ")\n";
    std::cout << "[STATE] Final filtered state: (" << cachedState.x << ", " << cachedState.y << ", "
              << cachedState.z << ", " << cachedState.w << ")\n";

    std::cout << "\n[SIGMA POINTS] (9 points):\n";
    for (int i = 0; i < 9; ++i) {
        std::cout << "  Sigma[" << i << "]: (" << cachedSigmaPoints[i].x << ", " << cachedSigmaPoints[i].y
                  << ", " << cachedSigmaPoints[i].z << ", " << cachedSigmaPoints[i].w << ")\n";
    }

    std::cout << "\n[Z-SIGMA POINTS] (Measurement space):\n";
    for (int i = 0; i < 9; ++i) {
        std::cout << "  ZSigma[" << i << "]: (" << cachedZSigmaPoints[i].x << ", " << cachedZSigmaPoints[i].y << ")\n";
    }

    std::cout << "\n[COVARIANCE MATRIX]:\n";
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            std::cout << cachedCovariance[j][i] << " ";
        }
        std::cout << "\n";
    }
    std::cout << "==========================================\n\n";
}

void Parse::computeLoop() {
    std::cout << "[+] Kalman compute thread started\n";

    while (threadRunning) {
        std::unique_lock<std::mutex> lock(measurementMutex);
        measurementCV.wait(lock, [this] { return newMeasurementAvailable || !threadRunning; });

        if (!threadRunning) break;

        std::cout << "[*] Processing measurement: (" << currentMeasurement.x << ", " << currentMeasurement.y << ")\n";

        recordComputeCommand();
        submitAndWait();
        readResultsFromGPU();

        newMeasurementAvailable = false;
    }
}

void Parse::updateMeasurements(glm::vec2 measurement) {
    std::lock_guard<std::mutex> lock(measurementMutex);
    currentMeasurement = measurement;
    newMeasurementAvailable = true;
    measurementCV.notify_one();
}

glm::vec4 Parse::getState() {
    std::lock_guard<std::mutex> lock(stateMutex);
    return cachedState;
}

glm::mat4 Parse::getCovariance() {
    std::lock_guard<std::mutex> lock(stateMutex);
    return cachedCovariance;
}

std::vector<glm::vec4> Parse::getSigmaPoints() {
    std::lock_guard<std::mutex> lock(stateMutex);
    return cachedSigmaPoints;
}

std::vector<glm::vec2> Parse::getZSigmaPoints() {
    std::lock_guard<std::mutex> lock(stateMutex);
    return cachedZSigmaPoints;
}

uint32_t Parse::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

void Parse::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        std::cerr << "[ERROR] Failed to create buffer\n";
        return;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        std::cerr << "[ERROR] Failed to allocate memory\n";
        return;
    }

    vkBindBufferMemory(device, buffer, memory, 0);
}

void Parse::copyBufferToStaging(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer copyCmd;
    vkAllocateCommandBuffers(device, &allocInfo, &copyCmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(copyCmd, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(copyCmd, srcBuffer, dstBuffer, 1, &copyRegion);

    vkEndCommandBuffer(copyCmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &copyCmd;

    vkQueueSubmit(computeQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(computeQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &copyCmd);
}
