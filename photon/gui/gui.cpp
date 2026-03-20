#include "gui.hpp"

#include <cfloat>
#include <cmath>

#include "imgui.h"
#include "implot.h"
#include "implot3d.h"


void GUI::buildUI(){
    ImGui::NewFrame();
    ImGui::ShowDemoWindow();
    ImPlot3D::ShowDemoWindow();
    backgroundWindow();
    ImGui::Render();
};

VkExtent2D quantizeContentExtent(ImVec2 contentSize, VkExtent2D fallback) {
    if (contentSize.x <= 1.0f || contentSize.y <= 1.0f) return fallback;
    const uint32_t width = std::max(1u, static_cast<uint32_t>(std::lround(contentSize.x)));
    const uint32_t height = std::max(1u, static_cast<uint32_t>(std::lround(contentSize.y)));
    return {width, height};
}

void GUI::backgroundWindow(){
    if(!backgroundShader.initialized || backgroundShader.frames.empty() || backgroundShader.frameIndex == nullptr) return;
    shaderFrame& frame = backgroundShader.frames[*backgroundShader.frameIndex];
    ImGui::SetNextWindowSize(ImVec2(frame.extent.width, frame.extent.height), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration;
    if(ImGui::Begin("background", NULL, 0)){
        const VkExtent2D nextExtent = quantizeContentExtent(ImGui::GetContentRegionAvail(), frame.extent);
        if (nextExtent.width != frame.extent.width || nextExtent.height != frame.extent.height) {
            frame.extent = nextExtent;
            backgroundShader.dirty = true;
        }
        ImVec2 drawSize(frame.extent.width, frame.extent.height);
        drawSize.x = std::max(drawSize.x, 1.0f);
        drawSize.y = std::max(drawSize.y, 1.0f);
        ImGui::Image(frame.texture, drawSize);
    }
    ImGui::End();
};

void GUI::shaderWindow(){

};

void GUI::gltfWindow(){

};

void GUI::processEvents(SDL_Event* events){
    if(events == nullptr) return;
    ImGuiIO& io = ImGui::GetIO();
    switch(events->type){
        case SDL_EVENT_MOUSE_MOTION:
            io.AddMousePosEvent(events->motion.x, events->motion.y); break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const int button = sdlMouseButtonToImGuiButton(events->button.button);
            if(button >= 0){
                io.AddMousePosEvent(events->button.x, events->button.y);
                io.AddMouseButtonEvent(button, events->button.down);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            float wheelX = events->wheel.x;
            float wheelY = events->wheel.y;
            if(events->wheel.direction == SDL_MOUSEWHEEL_FLIPPED){
                wheelX = -wheelX;
                wheelY = -wheelY;
            }
            io.AddMouseWheelEvent(wheelX, wheelY);
            break;
        }
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            io.AddFocusEvent(true); break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            io.AddFocusEvent(false); break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            addModifierEvents(io, events->key.mod);
            const ImGuiKey key = sdlScancodeToImGuiKey(events->key.scancode);
            if(key != ImGuiKey_None){
                io.AddKeyEvent(key, events->key.down);
                io.SetKeyEventNativeData(key, events->key.key, events->key.scancode, events->key.scancode);
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            if(events->text.text != nullptr) io.AddInputCharactersUTF8(events->text.text);
            break;
        default: break;
    }
}
