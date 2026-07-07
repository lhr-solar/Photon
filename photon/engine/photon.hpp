#pragma once
#include "../io/pre_fault_recorder.hpp"
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
  io::Pre_Fault_Recorder recorder{};
  bool running = true;
  double deltaTime = 16.67 * 1000;
  void init();
  void handleInput();
  void appLogic();
  void renderLoop();
  void destroy();
  bool reloadUI();
};
