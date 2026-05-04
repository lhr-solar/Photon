#pragma once
#include "imgui.h"
#include "string_view"

struct Widget{
    static void animTextBox(std::string_view text, bool start);
    static void animLine(ImVec2 begin, ImVec2 end, bool start);
};
