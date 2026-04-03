#pragma once
#include "arena.hpp"

struct Parse{
    Arena arena{};
    void init();
    void buildConfig(const std::string& path, arenaConfig& config);
    void populateArena(const std::string& path);
    void destroy();
};
