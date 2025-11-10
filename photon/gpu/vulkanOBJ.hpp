#pragma once
#include "obj_frag_spv.hpp"
#include "obj_vert_spv.hpp"
#include "vulkanDevice.hpp"
#include "vulkanShader.hpp"


struct OBJ{
    OBJ();
    ~OBJ();
    void init(VulkanDevice);
};
