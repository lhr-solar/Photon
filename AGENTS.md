# Repository Guidelines

## Architecture Overview
Photon is a C++23 CMake app for vehicle telemetry, rendering, and UI. The `Photon` target enters at
`photon/engine/main.cpp`. Subsystems live in `photon/`: `engine` coordinates runtime flow,
`network` ingests streams, `parse` decodes and stores CAN/DBC data in `Arena`, `gpu` handles
Vulkan/rendering, `gui` owns ImGui interfaces, and `synth` contains analysis/synthesis logic.

## Dashboard-Specific Guidance
Dashboard requests usually target `photon/gui/DDash/`: `ui.h` integrates, `dashboard.cpp` lays out
and renders, `state.h` defines UI state, `arena_bridge.cpp` maps `Arena` telemetry into `AppState`,
`theme.*` controls colors/fonts/spacing, and `widgets.*` holds reusable ImGui controls.
`UpdateDashboardState` mirrors DBC signals into `AppState::signals` by signal name; prefer
`state.get("Signal_Name")` for raw telemetry and add typed fields only for derived, multi-signal, or
UI-only state. Keep faults driver-focused: BPS takes priority, VCU faults are aggregated, and stale
CAN data is tracked through message age.
For layout changes, check desktop/kiosk sizing, avoid overlaps, and keep the dark ImGui theme.

## Project Structure
Source is under `photon/`, assets under `assets/`, docs under `docs/`, helpers under `find/`, and
vendored dependencies under `.external/`. Do not edit `.external/` unless updating a dependency.
Build outputs belong in `.artifacts/` or `.artifacts-linux/`.

## Build, Test, and Development Commands
Configure on Windows with clang and Ninja:

```powershell
cmake -S . -B .artifacts -G Ninja -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++
```

Build with `cmake --build .artifacts -j20`, then run `.artifacts/bin/Photon.exe`. For release, add
`-D CMAKE_BUILD_TYPE=Release`. Format with
`cmake --build .artifacts --target format`; check with `format-check`. Run CTest with:

```powershell
ctest --test-dir .artifacts --output-on-failure
```

## Coding Style & Naming Conventions
Follow `.clang-format`: Google C++ style, 100 columns, sorted includes, left pointer alignment. Keep
headers beside implementations and follow local names such as `gpu.cpp`, `sideBar.hpp`, and
`arena_bridge.cpp`. Keep CMake targets aligned with subsystem names.

## Testing Guidelines
No checked-in test directory is currently present; generated tests may appear in
`.artifacts/`. When adding tests, use `tests/` or a subsystem test folder, name files
`test_<feature>.cpp`, register them with CTest, and cover telemetry conversions, fault priority, and
UI state derivations for dashboard changes.

## Commit & Pull Request Guidelines
Recent commits use short imperative subjects, often `fix:` or scoped prefixes like `dash:`.
Example: `fix: default window size 1280x800`. PRs should describe behavior, list build/test
commands, link issues, and include screenshots for visible GUI/dashboard changes.

## Security & Configuration Tips
Do not commit local paths, generated binaries, or private configuration. Treat `config.ini` as
user-editable runtime configuration and document new keys in `docs/` or the README.
