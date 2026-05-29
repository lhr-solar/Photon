# Photon repository instructions

## Build, test, and lint commands

### Windows
- `.\winBuild.ps1` builds the default `Debug` configuration with CMake/NMake, copies `artifacts\photon\Photon.exe` to the repo root as `Photon.exe`, and launches it.
- `.\winBuild.ps1 Release` builds the `Release` configuration.
- `.\winBuild.ps1 dash` builds the `DashboardOnly` target, copies `artifacts\photon\DashboardOnly.exe` to the repo root, and launches it.
- `.\winBuild.ps1 clean` removes `.cache`, `artifacts`, `Photon.exe`, and `DashboardOnly.exe`.
- `.\winBuild.ps1 help` prints the supported modes.

### Linux
- `./build` builds the default `Debug` configuration with CMake/Ninja, moves `artifacts/photon/Photon` to the repo root as `Photon`, and launches it.
- `./build Release` builds the `Release` configuration.
- `./build clean` removes `.cache`, `artifacts`, and the root `Photon` binary.
- `./build gdb` opens the built `Photon` binary in `gdb -tui`.
- `piInstall.sh` installs the Vulkan-related Linux packages the project expects on Raspberry Pi / Debian-like systems.

### Tests and lint
- No automated test suite, single-test runner, or lint target is currently configured in CMake or the repository scripts. Do not invent `ctest`, `clang-tidy`, or formatter commands that are not already present.

## High-level architecture

- The top-level `CMakeLists.txt` builds three major pieces: `kernels/` generates SPIR-V shader headers into `build/generated`, `fonts/` generates embedded font headers into the same directory, and `photon/` builds the runtime modules and executables. `gpu` and `gui` both include those generated headers directly.
- `photon/engine` is the real application orchestrator. The `Photon` executable is just a thin platform-specific entry point in `photon/engine/main.cpp`; the `Photon` class owns `Gpu`, `Gui`, `Network`, `Parse`, and `Synth`, then runs the startup sequence `initVulkan -> initWindow -> prepareScene -> initThreads -> renderLoop`.
- `DashboardOnly` is not a separate lightweight app stack. It links the same `engine` library and reuses the same Vulkan/ImGui runtime, but its entry point sets `gui.ui.dashboardOnly = true` so scene-specific GPU setup and the extra debug/demo panels are skipped.
- Runtime telemetry flow spans multiple modules: `engine` starts background network threads, loads every `.dbc` file from `./dbc`, `network` decodes CAN frames and stores physical values in `parsedSignals`, and `gui/ui.cpp` pulls those exact signal names into the DDash `ui::AppState` each frame before rendering the dashboard.
- The platform split is important when tracing bugs: Linux uses `candump -L can0` in `Network::candumpParser`, while Windows starts TCP producer/parser threads against the default host `3.141.38.115:9000`.

## Key conventions

- Add new shaders in `kernels/CMakeLists.txt` under `KERNEL_SOURCES`; the build converts them to `.spv` and then to generated header files. Add new embedded fonts in `fonts/CMakeLists.txt` under `FONT_FILES` instead of loading them ad hoc at runtime.
- Prefer wiring new app behavior through the existing `Photon` object graph (`Photon -> Gui/Gpu/Network/...`) rather than introducing new globals. DDash state is intentionally funneled through `ui::AppState` and updated inside `UI::build()`.
- Treat DBC signal names as part of the contract between networking and UI. `UI::build()` reads parsed values by exact string keys such as `MC_VehicleVelocity` and `VCU_BPS_FAULT_DETECTED`, so renaming signal identifiers requires coordinated changes to the DBC files and UI mapping code.
- Keep the `dashboardOnly` flag as the switch for dashboard-only behavior. Existing code uses that flag to skip uniform buffer/pipeline work and to suppress the extra ImPlot/demo windows rather than maintaining a separate rendering path.
- Use the `logs(...)` macro from `photon/engine/include.hpp` for application logging in engine-adjacent code. On Windows it attaches to the parent console so logs still appear even though the executables are built as GUI apps.
