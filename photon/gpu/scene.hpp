#pragma once
#include "gltf.hpp"

struct Scene {
    std::vector<Gltf> models{};
    void init();
    void render();
    void destroy();
};
