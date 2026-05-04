#pragma once
#include <vector>
#include <functional>
#include <string>
#include "imgui.h"

struct Tab{
    std::function<void(ImGuiWindowFlags)> function = {};
    std::string name = {};
};

struct Tabs{
    int index = 0;
    std::vector<Tab> list{};
};
