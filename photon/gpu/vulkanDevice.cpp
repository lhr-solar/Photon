#include <vulkan/vulkan.h>
#include <assert.h>
#include <algorithm>
#include <string.h>

#include "vulkanDevice.hpp"
#include "gpu.hpp"
#include "../engine/include.hpp"

auto printQueueFlags = [](VkQueueFlags flags){
    std::string out;
    if (flags & VK_QUEUE_GRAPHICS_BIT) out += "GRAPHICS ";
    if (flags & VK_QUEUE_COMPUTE_BIT)  out += "COMPUTE ";
    if (flags & VK_QUEUE_TRANSFER_BIT) out += "TRANSFER ";
    if (flags & VK_QUEUE_SPARSE_BINDING_BIT) out += "SPARSE_BINDING ";
    return out.empty() ? "UNKNOWN" : out;
};

VkResult VulkanDevice::initDevice(VkPhysicalDevice physicalDevice){
    assert(physicalDevice);
    this->physicalDevice = physicalDevice;

    /* store device properties*/
    vkGetPhysicalDeviceProperties(this->physicalDevice, &deviceProperties);
    vkGetPhysicalDeviceFeatures(this->physicalDevice, &deviceFeatures);
    vkGetPhysicalDeviceMemoryProperties(this->physicalDevice, &deviceMemoryProperties);

    /* store queue family properties */
    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(this->physicalDevice, &queueFamilyCount, nullptr);
    assert(queueFamilyCount > 0);
    queueFamilyProperties.resize(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(this->physicalDevice, &queueFamilyCount, queueFamilyProperties.data());
    log("[?] Queue Family Properties:");
    int index = 0;
    for(const auto& props : queueFamilyProperties){
        log("[+] Queue Family Index " << index++ << " -> queue count = " << props.queueCount << " | " << printQueueFlags(props.queueFlags));
    }

    /* store list of supported extensions */
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(this->physicalDevice, nullptr, &extCount, nullptr);
    if (extCount > 0){
        std::vector<VkExtensionProperties> extensions(extCount);
        log("[?] Device Extension Properties: ");
        if (vkEnumerateDeviceExtensionProperties(this->physicalDevice, nullptr, &extCount, &extensions.front()) == VK_SUCCESS){
            for (auto& ext : extensions){
                log("[+] " << ext.extensionName);
                supportedExtensions.push_back(ext.extensionName);
            }
        }
	}

    return VK_SUCCESS;
};



VkResult VulkanDevice::createLogicalDevice(VkPhysicalDeviceFeatures enabledFeatures, std::vector<const char*> enabledExtensions, void* pNextChain, 
                                            bool useSwapChain, VkQueueFlags requestedQueueTypes){

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};
    const float defaultQueuePriority(0.0f);

    // graphics queue
    if(requestedQueueTypes & VK_QUEUE_GRAPHICS_BIT){
        queueFamilyIndices.graphics = getQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT);
        log("[+] Graphics Queue using family index : " << queueFamilyIndices.graphics);
        VkDeviceQueueCreateInfo queueInfo{};
    	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamilyIndices.graphics;
	    queueInfo.queueCount = 1;
	    queueInfo.pQueuePriorities = &defaultQueuePriority;
    	queueCreateInfos.push_back(queueInfo);
    } else { queueFamilyIndices.graphics = 0; }

    // compute queue
    if(requestedQueueTypes & VK_QUEUE_COMPUTE_BIT){
        queueFamilyIndices.compute = getQueueFamilyIndex(VK_QUEUE_COMPUTE_BIT);
        log("[+] Compute Queue using family index : " << queueFamilyIndices.compute);
        if(queueFamilyIndices.compute != queueFamilyIndices.graphics){
            // If compute family index differs, we need an additional queue create info for the compute queue
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = queueFamilyIndices.compute;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &defaultQueuePriority;
            queueCreateInfos.push_back(queueInfo);
            }
		} else { queueFamilyIndices.compute = queueFamilyIndices.graphics; }

    // transfer queue
    if(requestedQueueTypes & VK_QUEUE_TRANSFER_BIT){ 
        queueFamilyIndices.transfer = getQueueFamilyIndex(VK_QUEUE_TRANSFER_BIT);
        log("[+] Transfer Queue using family index : " << queueFamilyIndices.transfer);
        if((queueFamilyIndices.transfer != queueFamilyIndices.graphics) && (queueFamilyIndices.transfer != queueFamilyIndices.compute)){
            // If transfer family index differs, we need an additional queue create info for the transfer queue
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = queueFamilyIndices.transfer;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &defaultQueuePriority;
            queueCreateInfos.push_back(queueInfo);
		} 
    } else { queueFamilyIndices.transfer = queueFamilyIndices.graphics; }

    // create logical device representation
    std::vector<const char*> deviceExtensions(enabledExtensions); 
    if (useSwapChain) deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());;
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
    deviceCreateInfo.pEnabledFeatures = &enabledFeatures;

    // chain modern features -- maybe worth looking into?
    VkPhysicalDeviceFeatures2 physicalDeviceFeatures2{};
    if (pNextChain) {
        physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        physicalDeviceFeatures2.features = enabledFeatures;
        physicalDeviceFeatures2.pNext = pNextChain;
        deviceCreateInfo.pEnabledFeatures = nullptr;
        deviceCreateInfo.pNext = &physicalDeviceFeatures2;
    }

    if(deviceExtensions.size() > 0){
        for(const char * enabledExtension : deviceExtensions){
            if(!extensionSupported(enabledExtension)){ log("[!] Enabled device extension " << enabledExtension << " is not present at device level"); }
        }

        deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
    }

    log("[?] Enabled Device Extensions:");
    for(int i = 0; i < deviceCreateInfo.enabledExtensionCount; i++){
        log("[+] " << deviceCreateInfo.ppEnabledExtensionNames[i]);
    }

    VkResult result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &logicalDevice);
    if(result != VK_SUCCESS)
        return result;

    // create command pool for graphics command buffers
    VkCommandPoolCreateFlags createFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    graphicsCommandPool = createCommandPool(queueFamilyIndices.graphics, createFlags);

    // TODO possibly transient command buffers?
    if(queueFamilyIndices.graphics != queueFamilyIndices.compute)
        computeCommandPool = createCommandPool(queueFamilyIndices.compute, createFlags);

    return result;
};

uint32_t VulkanDevice::getQueueFamilyIndex(VkQueueFlags queueFlags) const{
    // TODO improve queue selection algorithm
    // Dedicated queue for compute
    if((queueFlags & VK_QUEUE_COMPUTE_BIT) == queueFlags){
	    for(uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++){
            if((queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0))
                { return i; }
        }
    }
    // Dedicated queue for transfer
    if((queueFlags & VK_QUEUE_TRANSFER_BIT) == queueFlags){
        for(uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++){
            if((queueFamilyProperties[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0))
            { return i; }
        }
    }

    // For other queue types or if no separate compute queue is present, return the first one to support the requested flags   
    for(uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++){
        if((queueFamilyProperties[i].queueFlags & queueFlags) == queueFlags)
        { return i; }
	}

    fatal("[!] Could not find a matching queue family index", -1);
};

bool VulkanDevice::extensionSupported(std::string extension){ 
    return (std::find(supportedExtensions.begin(), supportedExtensions.end(), extension) != supportedExtensions.end());
}

VkCommandPool VulkanDevice::createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags){
        VkCommandPoolCreateInfo cmdPoolInfo = {};
		cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
		cmdPoolInfo.flags = createFlags;
		VkCommandPool cmdPool;
		VK_CHECK(vkCreateCommandPool(logicalDevice, &cmdPoolInfo, nullptr, &cmdPool));
		return cmdPool;
}

uint32_t VulkanDevice::getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 * memTypeFound) const{
    for (uint32_t i = 0; i < deviceMemoryProperties.memoryTypeCount; i++){
		if ((typeBits & 1) == 1){
            if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties){
                if (memTypeFound){
                    *memTypeFound = true;
                }
					return i;
            }
        }
        typeBits >>= 1;
    }

    if (memTypeFound){
        *memTypeFound = false;
        return 0;
	} else {
        fatal("Could not find a matching memory type" , -1);
    }
}

/**
* Create a buffer on the device
*
* @param usageFlags Usage flag bit mask for the buffer (i.e. index, vertex, uniform buffer)
* @param memoryPropertyFlags Memory properties for this buffer (i.e. device local, host visible, coherent)
* @param buffer Pointer to a VulkanBuffer object
* @param size Size of the buffer in bytes
* @param data Pointer to the data that should be copied to the buffer after creation (optional, if not set, no data is copied over)
*
* @return VK_SUCCESS if buffer handle and memory have been created and (optionally passed) data has been copied
*/
VkResult VulkanDevice::createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VulkanBuffer *buffer, VkDeviceSize size, void *data){
    buffer->device = logicalDevice;

    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.usage = usageFlags;
    bufferCreateInfo.size = size;
    VK_CHECK(vkCreateBuffer(logicalDevice, &bufferCreateInfo, nullptr, &buffer->buffer));
    log("[+] Created Buffer of size " << size << " with flags 0x"  << std::hex << usageFlags << std::dec);

    VkMemoryRequirements memReqs;
    VkMemoryAllocateInfo memAlloc{};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vkGetBufferMemoryRequirements(logicalDevice, buffer->buffer, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, memoryPropertyFlags, nullptr);

    // Important: Check for VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    VkMemoryAllocateFlagsInfoKHR allocFlagsInfo{};
    if (usageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
			allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
			allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
			memAlloc.pNext = &allocFlagsInfo;
	}
    VK_CHECK(vkAllocateMemory(logicalDevice, &memAlloc, nullptr, &buffer->memory));
    log("[+] Allocated Buffer of size " << memAlloc.allocationSize );

    buffer->alignment = memReqs.alignment;
    buffer->size = size;
    buffer->usageFlags = usageFlags;
    buffer->memoryPropertyFlags = memoryPropertyFlags;

    if(data != nullptr){
        VK_CHECK(buffer->map(VK_WHOLE_SIZE, 0));
        memcpy(buffer->mapped, data, size);
        log("[+] copied payload to buffer");
        if ((memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0){
            buffer->flush(VK_WHOLE_SIZE, 0);
            log("[!] flushed buffer");
        }
        buffer->unmap();
        log("[-] unmapped buffer");
    }

    buffer->setupDescriptor(VK_WHOLE_SIZE, 0);

    return buffer->bind(0);
}
