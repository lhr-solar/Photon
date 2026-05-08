#pragma once
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

struct Tab {
  using InvokeFunction = void (*)(void*, ImGuiWindowFlags);

  void* owner = nullptr;
  InvokeFunction function = nullptr;
  std::string name = {};

  template <typename Owner, void (Owner::*Function)(ImGuiWindowFlags)>
  static Tab bind(Owner& owner, std::string name) {
    return Tab{.owner = &owner, .function = &invoke<Owner, Function>, .name = std::move(name)};
  }

  void draw(ImGuiWindowFlags flags) const {
    if (owner && function) function(owner, flags);
  }

 private:
  template <typename Owner, void (Owner::*Function)(ImGuiWindowFlags)>
  static void invoke(void* owner, ImGuiWindowFlags flags) {
    (static_cast<Owner*>(owner)->*Function)(flags);
  }
};

struct Tabs {
  int index = 0;
  std::vector<Tab> list{};
};
