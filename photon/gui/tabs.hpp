#pragma once
#include <vector>
#include <string>
#include "imgui.h"

template <typename Owner>
using TabFunction = void (Owner::*)(ImGuiWindowFlags);

template <typename Owner>
struct Tab{
    TabFunction<Owner> function = nullptr;
    std::string name = {};
};

template <typename Owner>
struct Tabs{
    int index = 0;
    std::vector<Tab<Owner>> list{};
};
