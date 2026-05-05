#pragma once
#include <vector>
#include <string>
#include "imgui.h"

using TabFunction = void (*)(ImGuiWindowFlags);

struct Tab{
    TabFunction function = nullptr;
    std::string name = {};
};

struct Tabs{
    int index = 0;
    std::vector<Tab> list{};
};
