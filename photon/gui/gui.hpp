#pragma once
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "../gpu/shader.hpp"
#include "../gpu/gltf.hpp"
#include "../gpu/scene.hpp"
#include "../network/network.hpp"
#include "../parse/parse.hpp"

enum class WindowAction{
    None,
    Close,
    Minimize,
    ToggleMaximize,
};

struct TitleBar{
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

struct Pages{
    struct Page{
        std::string label;
        std::function<void()> render;
    };
    std::vector<Page> entries{};
    size_t activePage = 0;
    ImGuiID dockspaceID = 0;

    template <typename Fn>
    size_t addPage(const char* label, Fn&& render) {
        entries.push_back(Page{label, std::forward<Fn>(render)});
        return entries.size() - 1;
    }

    void removePage(size_t index){
        if (index >= entries.size()) return;
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
        if (entries.empty()) {
            activePage = 0;
            return;
        }
        if (activePage >= entries.size()) activePage = entries.size() - 1;
    }

    void showPage(size_t index){
        if (index >= entries.size()) return;
        activePage = index;
        if (dockspaceID != 0) ImGui::SetNextWindowDockID(dockspaceID, ImGuiCond_FirstUseEver);
        if (entries[index].render) entries[index].render();
    }
};

struct GUI{
    Network* network{};
    Parse* parse{};
    GPU* gpu{};
    GUICommandQueue guiCommands{};
    GUIResponseQueue::Reader guiResponses{};
    Shader backgroundShader{};
    Gltf carModel{};
    Scene sceneModel{};
    SDL_Window* window = nullptr;
    TitleBar titleBar{};
    Pages pages{};
    float leftPaneWidth = 0.0f;
    ProtocolKind pendingProtocol = ProtocolKind::TCP;
    std::string networkStatus = "idle";
    std::vector<std::string> handlerMessages{};
    void init(GPU* gpu, Network* network, Parse* parse);
    void handleNetwork();
    void bindNetworkResponses(GUIResponseQueue::Reader reader);
    void queueStartProtocol(ProtocolKind kind);
    void queueStopProtocol();
    void buildUI();
    void bindWindow(SDL_Window* targetWindow);
    void buildTitleBar();
    void processEvents(SDL_Event* events);
    void leftSideBar();
    void resizeHorizontalLayout();
    void backgroundWindow();
    void gltfWindow();
    void networkWindow();
    void sceneWindow();
    void shaderWindow();
    void dockspace();
    void setStyle();
    void newStyle();
    void demoPlots();
    static ImGuiKey sdlToImgui(SDL_Scancode scancode);
    static inline void addModifier(ImGuiIO& io, SDL_Keymod mod);
    static inline int sdlMouseToImgui(Uint8 button);
};
