#include "vulkanOBJ.hpp"
#include "obj_frag_spv.hpp"
#include "obj_vert_spv.hpp"
#include "vulkanDevice.hpp"
#include "vulkan_core.h"
#include "tiny_obj_loader.h"

OBJ::OBJ(){
}

OBJ::~OBJ(){
}

void OBJ::init(VulkanDevice device){
    VkShaderModule fragShaderModule;
    VkShaderModuleCreateInfo fragCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = obj_frag_spv_size,
        .pCode = obj_frag_spv,
    };
    VkShaderModule vertShaderModule;
    VkShaderModuleCreateInfo vertCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = obj_vert_spv_size,
        .pCode = obj_vert_spv,
    };

    vkCreateShaderModule(device.logicalDevice, &fragCI, NULL, &fragShaderModule);
    vkCreateShaderModule(device.logicalDevice, &fragCI, NULL, &vertShaderModule);

}
