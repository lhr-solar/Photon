#pragma once
#include "gpu.hpp"
#include "gui.hpp"
#include "include.hpp"
#include "network.hpp"
#include "parse.hpp"
#include "synth.hpp"

struct Photon {
  GPU gpu{};
  GUI gui{};
  Network network{};
  Parse parse{};
  bool running = true;
  double deltaTime = 16.67 * 1000;
  std::string version = "00.00.01";
  void init();
  void handleInput();
  void appLogic();
  void renderLoop();
  void destroy();
  bool reloadUI();
};
