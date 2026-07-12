#pragma once

#include <atomic>
#include <filesystem>
#include <memory>

#include "../parse/arena.hpp"

struct Exporter {
  void toFile(Arena& arena, std::filesystem::path filePath = "output.csv");
  std::atomic<bool> running = false;
};
