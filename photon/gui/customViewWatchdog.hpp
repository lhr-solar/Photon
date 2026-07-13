#pragma once

#include "../parse/arena.hpp"
#include "customViewTypes.hpp"

struct CustomViewWatchdogWidget {
  static void draw(Arena* arena, CustomViewWidget& widget);
};
