#include <vulkan/vulkan.h>
#include <assert.h>
#include <algorithm>
#include <stdlib.h>
#include "vulkanSwapchain.hpp"
#include "gpu.hpp"
#include "../engine/include.hpp"
#include "vulkan/vulkan_core.h"

void VulkanSwapchain::initSurface(VkInstance instance, xcb_connection_t* connection, xcb_window_t window, VkPhysicalDevice physicalDevice){

    VkXcbSurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.connection = connection;
	surfaceCreateInfo.window = window;
    VK_CHECK(vkCreateXcbSurfaceKHR(instance, &surfaceCreateInfo, nullptr, &this->surface));

	uint32_t queueCount;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, NULL);
	assert(queueCount >= 1);

	std::vector<VkQueueFamilyProperties> queueProps(queueCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queueProps.data());

	// Iterate over each queue to learn whether it supports presenting:
	// Find a queue with present support
	// Will be used to present the swap chain images to the windowing system
	std::vector<VkBool32> supportsPresent(queueCount);
	for(uint32_t i = 0; i < queueCount; i++) { vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportsPresent[i]);}

	// Search for a graphics and a present queue in the array of queue
	// families, try to find one that supports both
	uint32_t graphicsQueueNodeIndex = UINT32_MAX;
	uint32_t presentQueueNodeIndex = UINT32_MAX;
	for(uint32_t i = 0; i < queueCount; i++){
        if((queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0){ 
            if (graphicsQueueNodeIndex == UINT32_MAX){
				graphicsQueueNodeIndex = i;
			}
			if (supportsPresent[i] == VK_TRUE){
				graphicsQueueNodeIndex = i;
				presentQueueNodeIndex = i;
				break;
			}
		}
	}
	if(presentQueueNodeIndex == UINT32_MAX){	
		// If there's no queue that supports both present and graphics
		// try to find a separate present queue
		for (uint32_t i = 0; i < queueCount; ++i){
			if (supportsPresent[i] == VK_TRUE){
				presentQueueNodeIndex = i;
				break;
			}
		}
	}
    if (graphicsQueueNodeIndex == UINT32_MAX || presentQueueNodeIndex == UINT32_MAX){
		fatal("Could not find a graphics and/or presenting queue!", -1);}

	if (graphicsQueueNodeIndex != presentQueueNodeIndex){
		fatal("Separate graphics and presenting queues are not supported yet!", -1);}

    surfaceQueueNodeIndex = graphicsQueueNodeIndex;
    log("[+] Surface utilizing queue : " << surfaceQueueNodeIndex);

    uint32_t formatCount;
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, NULL));
	assert(formatCount > 0);

    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data()));

    // We want to get a format that best suits our needs, so we try to get one from a set of preferred formats
	// Initialize the format to the first one returned by the implementation in case we can't find one of the preffered formats
	VkSurfaceFormatKHR selectedFormat = surfaceFormats[0];
	std::vector<VkFormat> preferredImageFormats = { 
		VK_FORMAT_B8G8R8A8_UNORM,
		VK_FORMAT_R8G8B8A8_UNORM, 
		VK_FORMAT_A8B8G8R8_UNORM_PACK32 
	};

	for(auto& availableFormat : surfaceFormats){
		if(std::find(preferredImageFormats.begin(), preferredImageFormats.end(), availableFormat.format) != preferredImageFormats.end()) {
			selectedFormat = availableFormat;
			break;
		}
	}

    surfaceFormat.format = selectedFormat.format;
    surfaceFormat.colorSpace = selectedFormat.colorSpace;
    log("[+] Using Surface Format : " << surfaceFormat.format);
    log("[+] Using Surface Color Space : " << surfaceFormat.colorSpace);
}

void VulkanSwapchain::createSurfaceCommandPool(VkDevice device){
    VkCommandPoolCreateInfo cmdPoolInfo = {};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.queueFamilyIndex = surfaceQueueNodeIndex;
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &surfaceCommandPool));
    log("[+] Created Command Pool for Queue Node Index " << surfaceQueueNodeIndex);
}

VkResult VulkanSwapchain::createSwapChain(uint32_t *width, uint32_t *height, bool vsync, bool fullscreen, bool transparent, VkPhysicalDevice physicalDevice, VkDevice device){
    VkSwapchainKHR oldSwapChain = swapChain;

    VkSurfaceCapabilitiesKHR surfCaps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfCaps));

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, NULL);
    assert(presentModeCount > 0);

    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data()));

    VkExtent2D swapchainExtent = {};
    // if width & height = UINT32_MAX, the size of the surface is set by the swapchain
    if(surfCaps.currentExtent.width == (uint32_t)-1){
        swapchainExtent.width = *width;
        swapchainExtent.height = *height;
    } else {
        swapchainExtent = surfCaps.currentExtent;
        *width = surfCaps.currentExtent.width;
        *height = surfCaps.currentExtent.height;
    }

    // spec required present mode
    VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

    // otherwise use lower-latency present mode
    if(!vsync){
        for (size_t i = 0; i < presentModeCount; i++){
            if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR){
                swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
				break;
			}
			if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
                swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
	    }
    }
    log("[+] Using Swap Chain present mode : " << swapchainPresentMode);

    // set number of images
    uint32_t desiredNumberOfSwapchainImages = surfCaps.minImageCount + 1;
    if ((surfCaps.maxImageCount > 0) && (desiredNumberOfSwapchainImages > surfCaps.maxImageCount))
        desiredNumberOfSwapchainImages = surfCaps.maxImageCount;
    log("[+] Number of Swap Chain Images : " << desiredNumberOfSwapchainImages);


    // prefer a non-rotated transform
    VkSurfaceTransformFlagsKHR preTransform;
    if (surfCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR){ 
        preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    } else {
		preTransform = surfCaps.currentTransform;
	}
    log("[+] Using Swap Chain Transform : " << preTransform);

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    std::vector<VkCompositeAlphaFlagBitsKHR> compositeAlphaFlags = {
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
	};
    for (auto& compositeAlphaFlag : compositeAlphaFlags){
		if(surfCaps.supportedCompositeAlpha & compositeAlphaFlag){
			compositeAlpha = compositeAlphaFlag;
			break;
		};
	}
    log("[+] Using Composite Alpha : " << compositeAlpha);

    VkSwapchainCreateInfoKHR swapchainCI = {};
	swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainCI.surface = surface;
	swapchainCI.minImageCount = desiredNumberOfSwapchainImages;
	swapchainCI.imageFormat = surfaceFormat.format;
	swapchainCI.imageColorSpace = surfaceFormat.colorSpace;
	swapchainCI.imageExtent = { swapchainExtent.width, swapchainExtent.height };
	swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainCI.preTransform = (VkSurfaceTransformFlagBitsKHR)preTransform;
	swapchainCI.imageArrayLayers = 1;
	swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainCI.queueFamilyIndexCount = 0;
	swapchainCI.presentMode = swapchainPresentMode;
	// Setting oldSwapChain to the saved handle of the previous swapchain aids in resource reuse and makes sure that we can still present already acquired images
	swapchainCI.oldSwapchain = oldSwapChain;
	// Setting clipped to VK_TRUE allows the implementation to discard rendering outside of the surface area
	swapchainCI.clipped = VK_TRUE;
	swapchainCI.compositeAlpha = compositeAlpha;

    // Enable transfer source on swap chain images if supported
	if (surfCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
        log("[+] Swap Chain using Transfer Source");
		swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}

	// Enable transfer destination on swap chain images if supported
	if (surfCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
        log("[+] Swap Chain using Transfer Destination");
		swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}

	VK_CHECK(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapChain));
    log("[+] Created Swap Chain");

    // delete the old swapchain
    if(oldSwapChain != VK_NULL_HANDLE){
        for(uint32_t i = 0; i < imageCount; i++){
            vkDestroyImageView(device, buffers[i].view, nullptr);
        }
        vkDestroySwapchainKHR(device, oldSwapChain, nullptr);
        log("[!] Destroyed Old Swap Chain");
    }

    // get the swap chain images
    VK_CHECK(vkGetSwapchainImagesKHR(device, swapChain, &imageCount, NULL));
    images.resize(imageCount);
    VK_CHECK(vkGetSwapchainImagesKHR(device, swapChain, &imageCount, images.data()));
    log("[+] Using " << imageCount << " Swap Chain images");

    // get the swap chain buffers containing the image and imageView
    buffers.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; i++){
		VkImageViewCreateInfo colorAttachmentView = {};
		colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		colorAttachmentView.pNext = NULL;
		colorAttachmentView.format = surfaceFormat.format;
		colorAttachmentView.components = {
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_B,
			VK_COMPONENT_SWIZZLE_A
		};
		colorAttachmentView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorAttachmentView.subresourceRange.baseMipLevel = 0;
		colorAttachmentView.subresourceRange.levelCount = 1;
		colorAttachmentView.subresourceRange.baseArrayLayer = 0;
		colorAttachmentView.subresourceRange.layerCount = 1;
		colorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorAttachmentView.flags = 0;

		buffers[i].image = images[i];

		colorAttachmentView.image = buffers[i].image;

		VK_CHECK(vkCreateImageView(device, &colorAttachmentView, nullptr, &buffers[i].view));
	}
    log("[+] Constructed Image View");

    return VK_SUCCESS;
}

void VulkanSwapchain::createSurfaceCommandBuffers(VkDevice device){
    // create one command buffer for each swap chain image
    drawCmdBuffers.resize(imageCount);
    VkCommandBufferAllocateInfo commandBufferAllocateInfo {};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.commandPool = surfaceCommandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = drawCmdBuffers.size();

    VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, drawCmdBuffers.data()));
    log("[+] Allocated " << drawCmdBuffers.size() << " Command Buffers with level : " << commandBufferAllocateInfo.level);
}

VkResult VulkanSwapchain::acquireNextImage(VkDevice device, VkSemaphore presentCompleteSemaphore, uint32_t* imageIndex){
    return vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, presentCompleteSemaphore, VkFence(nullptr) , imageIndex);
}

VkResult VulkanSwapchain::queuePresent(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore){
    VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = NULL;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapChain;
	presentInfo.pImageIndices = &imageIndex;
    // Check if a wait semaphore has been specified to wait for before presenting the image
	if (waitSemaphore != VK_NULL_HANDLE)
	{
		presentInfo.pWaitSemaphores = &waitSemaphore;
		presentInfo.waitSemaphoreCount = 1;
	}
	return vkQueuePresentKHR(queue, &presentInfo);
}
