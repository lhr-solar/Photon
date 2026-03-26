#pragma once
#include <array>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "../gpu/shader.hpp"
#include "../gpu/gltf.hpp"
#include "../gpu/scene.hpp"

enum class WindowAction {
    None,
    Close,
    Minimize,
    ToggleMaximize,
};

struct TitleBar {
    int height = 32;
    bool enabled = true;
    int interactiveRectCount = 0;
    WindowAction pendingAction = WindowAction::None;
    static constexpr int buttonCount = 3;
    std::array<SDL_Rect, buttonCount> interactiveRects{};
    void clearInteract();
    void addInteract(const ImVec2& min, const ImVec2& max);
    bool isInteract(int x, int y) const;
};

struct NetworkInterface {
    struct MetaData{
        std::string dataSource{};
        void* dataSourceMetaData{};
        std::string dbc{};
        std::string* availableDBC{};
        std::string ping{};
    };
    void* MemoryArena{};
};

struct GUI{
    Shader backgroundShader{};
    Gltf carModel{};
    Scene sceneModel{};
    SDL_Window* window = nullptr;
    TitleBar titleBar{};

    void buildUI();
    void bindWindow(SDL_Window* targetWindow);
    void buildTitleBar();
    void processEvents(SDL_Event* events);
    void backgroundWindow();
    void gltfWindow();
    void sceneWindow();
    void shaderWindow();
    void dockspace();
    void setStyle();
    void newStyle();
    static ImGuiKey sdlToImgui(SDL_Scancode scancode);
    static inline void addModifier(ImGuiIO& io, SDL_Keymod mod);
    static inline int sdlMouseToImgui(Uint8 button);
};
