#include "config.hpp"
GuiSettings* GuiSettings::get(ImGuiContext* ctx, ImGuiSettingsHandler*) {
    return static_cast<GuiSettings*>(ctx->IO.UserData);
}

void* GuiSettings::readOpenFn(ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name) {
    if (strcmp(name, "UI") != 0) return nullptr;
    return get(ctx, handler);
}

void GuiSettings::readLineFn(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line){
    GuiSettings* settings = static_cast<GuiSettings*>(entry);
    float value = 0.0f;
    if (sscanf(line, "FontSize=%f", &value) == 1)
        settings->fontSize = value;
}

void GuiSettings::writeAllFn(ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out_buf){
    GuiSettings* settings = get(ctx, handler);
    out_buf->appendf("[Photon][UI]\n");
    out_buf->appendf("FontSize=%.1f\n", settings->fontSize);
    out_buf->append("\n");
}

void GuiSettings::regster(GuiSettings* settings){
    ImGuiIO& io = ImGui::GetIO();
    io.UserData = settings;

    ImGuiSettingsHandler handler;
    handler.TypeName = "Photon";
    handler.TypeHash = ImHashStr("Photon");
    handler.ReadOpenFn = readOpenFn;
    handler.ReadLineFn = readLineFn;
    handler.WriteAllFn = writeAllFn;
    ImGui::AddSettingsHandler(&handler);
}
