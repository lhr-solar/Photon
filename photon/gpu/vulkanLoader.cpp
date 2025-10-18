#include "vulkanLoader.h"

// Define the pointers in a single translation unit
PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR pfn_vkGetPhysicalDeviceVideoCapabilitiesKHR = nullptr;
PFN_vkCreateVideoSessionKHR pfn_vkCreateVideoSessionKHR = nullptr;
PFN_vkDestroyVideoSessionKHR pfn_vkDestroyVideoSessionKHR = nullptr;
PFN_vkGetVideoSessionMemoryRequirementsKHR pfn_vkGetVideoSessionMemoryRequirementsKHR = nullptr;
PFN_vkBindVideoSessionMemoryKHR pfn_vkBindVideoSessionMemoryKHR = nullptr;
PFN_vkCreateVideoSessionParametersKHR pfn_vkCreateVideoSessionParametersKHR = nullptr;
PFN_vkDestroyVideoSessionParametersKHR pfn_vkDestroyVideoSessionParametersKHR = nullptr;
PFN_vkCmdBeginVideoCodingKHR pfn_vkCmdBeginVideoCodingKHR = nullptr;
PFN_vkCmdEndVideoCodingKHR pfn_vkCmdEndVideoCodingKHR = nullptr;
PFN_vkCmdDecodeVideoKHR pfn_vkCmdDecodeVideoKHR = nullptr;

void LoadVulkanVideoFunctions(VkInstance instance, VkDevice device)
{
    pfn_vkGetPhysicalDeviceVideoCapabilitiesKHR =
        (PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceVideoCapabilitiesKHR");

    pfn_vkCreateVideoSessionKHR =
        (PFN_vkCreateVideoSessionKHR)vkGetDeviceProcAddr(device, "vkCreateVideoSessionKHR");

    pfn_vkDestroyVideoSessionKHR =
        (PFN_vkDestroyVideoSessionKHR)vkGetDeviceProcAddr(device, "vkDestroyVideoSessionKHR");

    pfn_vkGetVideoSessionMemoryRequirementsKHR =
        (PFN_vkGetVideoSessionMemoryRequirementsKHR)vkGetDeviceProcAddr(device, "vkGetVideoSessionMemoryRequirementsKHR");

    pfn_vkBindVideoSessionMemoryKHR =
        (PFN_vkBindVideoSessionMemoryKHR)vkGetDeviceProcAddr(device, "vkBindVideoSessionMemoryKHR");

    pfn_vkCreateVideoSessionParametersKHR =
        (PFN_vkCreateVideoSessionParametersKHR)vkGetDeviceProcAddr(device, "vkCreateVideoSessionParametersKHR");

    pfn_vkDestroyVideoSessionParametersKHR =
        (PFN_vkDestroyVideoSessionParametersKHR)vkGetDeviceProcAddr(device, "vkDestroyVideoSessionParametersKHR");

    pfn_vkCmdBeginVideoCodingKHR =
        (PFN_vkCmdBeginVideoCodingKHR)vkGetDeviceProcAddr(device, "vkCmdBeginVideoCodingKHR");

    pfn_vkCmdEndVideoCodingKHR =
        (PFN_vkCmdEndVideoCodingKHR)vkGetDeviceProcAddr(device, "vkCmdEndVideoCodingKHR");

    pfn_vkCmdDecodeVideoKHR =
        (PFN_vkCmdDecodeVideoKHR)vkGetDeviceProcAddr(device, "vkCmdDecodeVideoKHR");
}