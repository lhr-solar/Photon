#pragma once
#include "protocols.hpp"
#include "../parse/arena.hpp"
#include <thread>

struct Network{
    std::jthread writer{};
    std::jthread reader{};
    Arena* arena{};
    void init(Arena* arena);
    void initThreads();
    void destroy();
};
