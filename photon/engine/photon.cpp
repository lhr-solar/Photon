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

#if !defined(NDEBUG)
#if defined(__linux__)
#include <dlfcn.h>
#elif defined(_WIN32)
#include <windows.h>
#endif
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
#if !defined(NDEBUG) && (defined(__linux__) || defined(_WIN32))
    namespace fs = std::filesystem;
    using BuildUI = bool (*)(GUI*);
    using DestroyUI = void (*)(GUI*);
    #if defined(_WIN32)
    using ModuleHandle = HMODULE;
    #else
    using ModuleHandle = void*;
    #endif
    static const fs::path buildLogPath = fs::path(PHOTON_BUILD_DIR) / "photon_ui_build.log";
    static const std::string kBuildCommand =
        "cmake --build \"" PHOTON_BUILD_DIR "\" --target photonUI --parallel > \"" + buildLogPath.string() + "\" 2>&1";
    struct State {
        ModuleHandle handle{};
        BuildUI build{};
        DestroyUI destroy{};
        fs::file_time_type loadedAt{};
        fs::file_time_type failedAt{};
        fs::path loadedPath{};
    };
    static State state{};
    static const fs::path soPath = PHOTON_UI_SO_PATH;
    static const fs::path uiDirPath = PHOTON_UI_DIR_PATH;
    const auto winErrorText = []() -> std::string {
#if defined(_WIN32)
        const DWORD errorCode = GetLastError();
        if (errorCode == 0) return {};
        LPSTR buffer = nullptr;
        const DWORD size = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            errorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPSTR>(&buffer),
            0,
            nullptr
        );
        std::string text = (size != 0 && buffer != nullptr) ? std::string(buffer, size) : ("Win32 error " + std::to_string(errorCode));
        if (buffer) LocalFree(buffer);
        return text;
#else
        return {};
#endif
    };
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
        if (state.destroy) state.destroy(&gui);
        if (state.handle) {
#if defined(_WIN32)
            FreeLibrary(state.handle);
#else
            dlclose(state.handle);
#endif
        }
        if (!state.loadedPath.empty()) {
            std::error_code ec;
            fs::remove(state.loadedPath, ec);
        }
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
        #if defined(_WIN32)
        state.handle = LoadLibraryW(loadPath.c_str());
        state.build = state.handle ? reinterpret_cast<BuildUI>(GetProcAddress(state.handle, "photonBuildUI")) : nullptr;
        state.destroy = state.handle ? reinterpret_cast<DestroyUI>(GetProcAddress(state.handle, "photonDestroyUI")) : nullptr;
        #else
        state.handle = dlopen(loadPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        state.build = state.handle ? reinterpret_cast<BuildUI>(dlsym(state.handle, "photonBuildUI")) : nullptr;
        state.destroy = state.handle ? reinterpret_cast<DestroyUI>(dlsym(state.handle, "photonDestroyUI")) : nullptr;
        #endif
        if (!state.build) {
            #if defined(_WIN32)
            uiErrorText = "UI load failed:\n" + winErrorText();
            #else
            uiErrorText = dlerror() ? std::string("UI load failed:\n") + dlerror() : "UI load failed";
            #endif
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
#if !defined(NDEBUG) && (defined(__linux__) || defined(_WIN32))
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
