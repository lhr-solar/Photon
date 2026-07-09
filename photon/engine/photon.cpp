#include "photon.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <tracy/Tracy.hpp>

#include "imgui.h"
#if defined(APPLE) || defined(__APPLE__)
#include "imgui_impl_sdl3.h"
#else
#include "../gui/io.hpp"
#endif

#if !defined(NDEBUG)
#if defined(__linux__)
#include <dlfcn.h>
#elif defined(_WIN32)
#include <windows.h>
#endif
#endif

void Photon::init() {
  gpu.init();
  logs("Initialized GPU");
  gpu.imguiBackend(&gui.titleBar);
  logs("Initialized ImGui");
  parse.init();
  logs("Initialized Arena");
  network.parse = &parse;
  network.init();
  logs("Initialized Network");
  gui.init(gpu, parse.arena, network);
  logs("Initialized GUI");
}

void Photon::renderLoop() {
  logs("Starting render loop");
  while (running) {
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

    gpu.frameIndex = (gpu.frameIndex + 1) % gpu.swapchainImages.size();
    auto endFrame = std::chrono::high_resolution_clock::now();
    deltaTime = std::chrono::duration<double, std::milli>(endFrame - startFrame).count();
  };
};

void Photon::destroy() {
#if !defined(APPLE) && !defined(__APPLE__)
  if (gpuAsyncDispatches.load(std::memory_order_relaxed) != 0) std::quick_exit(0);
#endif
  gui.destroy();
  gpu.destroy();
  network.destroy();
  parse.destroy();
};

void Photon::handleInput() {
  ImGuiIO& io = ImGui::GetIO();
  io.DeltaTime = deltaTime / 1000;
  const bool wantTextInput = io.WantTextInput;
  if (wantTextInput != SDL_TextInputActive(gpu.window)) {
    if (wantTextInput)
      SDL_StartTextInput(gpu.window);
    else
      SDL_StopTextInput(gpu.window);
  }
  SDL_Event events{};
  while (SDL_PollEvent(&events)) {
#if defined(APPLE) || defined(__APPLE__)
    ImGui_ImplSDL3_ProcessEvent(&events);
#endif
    if (events.type == SDL_EVENT_QUIT) running = false;
    if ((events.type == SDL_EVENT_WINDOW_RESIZED) ||
        (events.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) ||
        (events.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) ||
        (events.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED))
      gpu.resizePending = true;
#if !defined(APPLE) && !defined(__APPLE__)
    IO::handleInput(&events);
#endif
  }
};

static std::string uiErrorText{};
bool Photon::reloadUI() {
#if !defined(NDEBUG) && (defined(__linux__) || defined(_WIN32))
  namespace fs = std::filesystem;
  using BuildUI = bool (*)(GUI*);
#if defined(_WIN32)
  using ModuleHandle = HMODULE;
#else
  using ModuleHandle = void*;
#endif
  static const fs::path buildLogPath = fs::path(PHOTON_BUILD_DIR) / "photon_ui_build.log";
  static const std::string kBuildCommand = "cmake --build \"" PHOTON_BUILD_DIR
                                           "\" --target photonUI --parallel > \"" +
                                           buildLogPath.string() + "\" 2>&1";
  struct State {
    ModuleHandle handle{};
    BuildUI build{};
    fs::file_time_type loadedAt{};
    fs::file_time_type failedBinaryAt{};
    fs::file_time_type failedSourceAt{};
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
        nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string text = (size != 0 && buffer != nullptr)
                           ? std::string(buffer, size)
                           : ("Win32 error " + std::to_string(errorCode));
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
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec))
      return fs::file_time_type::min();
    fs::file_time_type latest = fs::file_time_type::min();
    for (const fs::directory_entry& entry : fs::directory_iterator(dirPath, ec)) {
      if (ec) break;
      if (!entry.is_regular_file(ec)) continue;
      const fs::path extension = entry.path().extension();
      if (extension != ".cpp" && extension != ".hpp" && extension != ".h" && extension != ".inl")
        continue;
      latest = std::max(latest, entry.last_write_time(ec));
      if (ec) break;
    }
    return latest;
  };
  const auto unloadUI = [](State& loaded) {
    if (loaded.handle) {
#if defined(_WIN32)
      FreeLibrary(loaded.handle);
#else
      dlclose(loaded.handle);
#endif
    }
    if (!loaded.loadedPath.empty()) {
      std::error_code ec;
      fs::remove(loaded.loadedPath, ec);
    }
    loaded = {};
  };
  const auto loadUI = [&](fs::file_time_type soAt, State& candidate) {
    const fs::path loadPath =
        soPath.parent_path() /
        (soPath.stem().string() + "." + std::to_string(soAt.time_since_epoch().count()) +
         soPath.extension().string());
    std::error_code ec;
    fs::copy_file(soPath, loadPath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      uiErrorText = "UI copy failed:\n" + ec.message();
      logs("UI copy failed: " << ec.message());
      return false;
    }
    logs("Loading UI: " << loadPath.string());
#if defined(_WIN32)
    candidate.handle = LoadLibraryA(loadPath.string().c_str());
    candidate.build =
        candidate.handle
            ? reinterpret_cast<BuildUI>(GetProcAddress(candidate.handle, "photonBuildUI"))
            : nullptr;
#else
    dlerror();
    candidate.handle = dlopen(loadPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    const char* loadError = dlerror();
    if (candidate.handle) {
      dlerror();
      candidate.build = reinterpret_cast<BuildUI>(dlsym(candidate.handle, "photonBuildUI"));
      if (const char* symbolError = dlerror()) loadError = symbolError;
    }
#endif
    if (!candidate.build) {
#if defined(_WIN32)
      uiErrorText = "UI load failed:\n" + winErrorText();
#else
      uiErrorText = loadError ? std::string("UI load failed:\n") + loadError : "UI load failed";
#endif
      candidate.loadedPath = loadPath;
      unloadUI(candidate);
      return false;
    }
    candidate.loadedAt = soAt;
    candidate.loadedPath = loadPath;
    uiErrorText.clear();
    return true;
  };

  const auto sourceAt = readSourceTreeTime(uiDirPath);
  auto soAt = readTime(soPath);
  if ((soAt == fs::file_time_type::min() || sourceAt > soAt) && sourceAt != state.failedSourceAt) {
    logs("Rebuilding UI: " << kBuildCommand);
    if (std::system(kBuildCommand.c_str()) != 0) {
      state.failedSourceAt = sourceAt;
      uiErrorText = readText(buildLogPath);
      if (uiErrorText.empty()) uiErrorText = "UI rebuild failed";
      logs("UI rebuild failed");
      return state.build ? state.build(&gui) : false;
    }
    soAt = readTime(soPath);
  }

  if ((!state.build || soAt != state.loadedAt) && soAt != state.failedBinaryAt) {
    State candidate{};
    if (!loadUI(soAt, candidate)) {
      state.failedBinaryAt = soAt;
      return state.build ? state.build(&gui) : false;
    }
    State previous = std::move(state);
    state = std::move(candidate);
    unloadUI(previous);
  }

  return state.build(&gui);
#else
  return false;
#endif
};

void Photon::appLogic() {
  ZoneScopedN("Photon::appLogic");
  handleInput();
#if !defined(NDEBUG) && (defined(__linux__) || defined(_WIN32))
  if (!reloadUI()) {
    ImGui::NewFrame();
    ImGui::TextUnformatted("UI Not Found...");
    if (!uiErrorText.empty()) {
      ImGui::Separator();
      ImGui::TextWrapped("%s", uiErrorText.c_str());
    }
    ImGui::Render();
  };
#else
  gui.buildUI();
#endif
};
