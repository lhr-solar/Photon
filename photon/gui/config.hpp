#pragma once
#include "imgui.h"
#include "imgui_internal.h"

struct GuiSettings{
    float fontSize = 28.0;
    static GuiSettings* get(ImGuiContext* ctx, ImGuiSettingsHandler*);
    static void* readOpenFn(ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name);
    static void readLineFn(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line);
    static void writeAllFn(ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out_buf);
    static void regster(GuiSettings* settings);
};

struct GuiFlags{
    bool showGPUInfo = false;
};
