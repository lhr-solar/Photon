#pragma once
#include "obj_frag_spv.hpp"
#include "obj_vert_spv.hpp"
#include "vulkanDevice.hpp"
#include "vulkanShader.hpp"

// at a minimum, what do we need?
// we need a shader that can bind to our ui as a descriptor
struct VulkanObj{
    bool dirty = false;
    void initObj();
    void createResources();
    void recordRenderPass();
};
