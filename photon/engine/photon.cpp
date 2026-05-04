#include "photon.hpp"
#include "../gui/io.hpp"
#include "imgui.h"
#include "vulkan_core.h"
#include <tracy/Tracy.hpp>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(__linux__) && !defined(NDEBUG)
#include <dlfcn.h>
#endif

static std::string uiErrorText{};

void Photon::init(){
    gpu.init();
    logs("Initialized GPU");
    gpu.imguiBackend(&gui.titleBar);
    logs("Initialized ImGui");
    parse.init();
    logs("Initialized Arena");
    gui.init(gpu);
    logs("Initialized GUI");
    network.init(&parse, &gui.networkCommandBuffer);
    logs("Initialized Network");

    updateApp();
}

void Photon::renderLoop(){
    logs("Starting render loop");
    while(running){
        FrameMark;
        ZoneScopedN("Photon::renderLoop");
        auto startFrame = std::chrono::high_resolution_clock::now();
        uint32_t imgIdx{};
        gpu.startFrame(imgIdx);
        gpu.startCommands();

        appLogic();
        gui.render();
        gpu.imguiPresentation(imgIdx);

        gpu.endCommands();
        gpu.submitFrame(imgIdx);

        gpu.frameIndex = (gpu.frameIndex+1)%gpu.swapchainImages.size();
        auto endFrame = std::chrono::high_resolution_clock::now();
        deltaTime = std::chrono::duration<double, std::milli>(endFrame - startFrame).count();
    };
};

void Photon::destroy(){
    if (gpuAsyncDispatches.load(std::memory_order_relaxed) != 0) std::quick_exit(0);
    gui.destroy();
    gpu.destroy();
    network.destroy();
    parse.destroy();
};

void Photon::handleInput(){
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = deltaTime / 1000;
    const bool wantTextInput = io.WantTextInput;
    if (wantTextInput != SDL_TextInputActive(gpu.window)) {
        if (wantTextInput) SDL_StartTextInput(gpu.window);
        else SDL_StopTextInput(gpu.window);
    }
    SDL_Event events{};
    while(SDL_PollEvent(&events)) {
        if (events.type == SDL_EVENT_QUIT) running = false;
        if ((events.type == SDL_EVENT_WINDOW_RESIZED) ||
            (events.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) ||
            (events.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) ||
            (events.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED))
                gpu.resizePending = true;
        IO::handleInput(&events);
    }
};

bool Photon::reloadUI(){
#if defined(__linux__) && !defined(NDEBUG)
    namespace fs = std::filesystem;
    using BuildUI = bool (*)(GUI*);
    static const fs::path buildLogPath = fs::path(PHOTON_BUILD_DIR) / "photon_ui_build.log";
    static const std::string kBuildCommand =
        "cmake --build \"" PHOTON_BUILD_DIR "\" --target photonUI --parallel > \"" + buildLogPath.string() + "\" 2>&1";
    struct State {
        void* handle{};
        BuildUI build{};
        fs::file_time_type loadedAt{};
        fs::file_time_type failedAt{};
        fs::path loadedPath{};
    };
    static State state{};
    static const fs::path soPath = PHOTON_UI_SO_PATH;
    static const fs::path uiDirPath = PHOTON_UI_DIR_PATH;
    const auto readText = [](const fs::path& path) {
        std::ifstream file(path);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    const auto readTime = [](const fs::path& path) {
        std::error_code ec;
        return fs::exists(path, ec) ? fs::last_write_time(path, ec) : fs::file_time_type::min();
    };
    const auto readSourceTreeTime = [](const fs::path& dirPath) {
        std::error_code ec;
        if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) return fs::file_time_type::min();
        fs::file_time_type latest = fs::file_time_type::min();
        for (const fs::directory_entry& entry : fs::directory_iterator(dirPath, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            const fs::path extension = entry.path().extension();
            if (extension != ".cpp" && extension != ".hpp" && extension != ".h" && extension != ".inl") continue;
            latest = std::max(latest, entry.last_write_time(ec));
            if (ec) break;
        }
        return latest;
    };
    const auto unloadUI = [&] {
        if (state.handle) dlclose(state.handle);
        if (!state.loadedPath.empty()) fs::remove(state.loadedPath);
        state = {};
    };
    const auto loadUI = [&](fs::file_time_type soAt) {
        const fs::path loadPath = soPath.parent_path() /
            (soPath.stem().string() + "." + std::to_string(soAt.time_since_epoch().count()) + soPath.extension().string());
        std::error_code ec;
        fs::copy_file(soPath, loadPath, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            uiErrorText = "UI copy failed:\n" + ec.message();
            logs("UI copy failed: " << ec.message());
            return false;
        }
        logs("Loading UI: " << loadPath.string());
        state.handle = dlopen(loadPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        state.build = state.handle ? reinterpret_cast<BuildUI>(dlsym(state.handle, "photonBuildUI")) : nullptr;
        if (!state.build) {
            uiErrorText = dlerror() ? std::string("UI load failed:\n") + dlerror() : "UI load failed";
            unloadUI();
            return false;
        }
        state.loadedAt = soAt;
        state.loadedPath = loadPath;
        state.failedAt = fs::file_time_type::min();
        uiErrorText.clear();
        return true;
    };

    const auto sourceAt = readSourceTreeTime(uiDirPath);
    auto soAt = readTime(soPath);
    if ((soAt == fs::file_time_type::min() || sourceAt > soAt) && sourceAt != state.failedAt) {
        logs("Rebuilding UI: " << kBuildCommand);
        if (std::system(kBuildCommand.c_str()) != 0) {
            state.failedAt = sourceAt;
            uiErrorText = readText(buildLogPath);
            if (uiErrorText.empty()) uiErrorText = "UI rebuild failed";
            logs("UI rebuild failed");
            return false;
        }
        soAt = readTime(soPath);
    }

    if (!state.build || soAt != state.loadedAt) {
        unloadUI();
        if (!loadUI(soAt)) return false;
    }

    return state.build(&gui);
#else
    return false;
#endif
};

void Photon::appLogic(){
    ZoneScopedN("Photon::appLogic");
    handleInput();
#if defined(__linux__) && !defined(NDEBUG)
    if(!reloadUI()){
        ImGui::NewFrame();
        ImGui::TextUnformatted("UI Not Found...");
        if(!uiErrorText.empty()){
            ImGui::Separator();
            ImGui::TextWrapped("%s", uiErrorText.c_str());
        }
        ImGui::Render();
    };
#else
    gui.buildUI();
#endif
};

void Photon::updateApp(){

};
