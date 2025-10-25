param(
    [ValidateSet("Debug", "Release", "Clean", "Help")]
    [string]$arg = "Debug"
)

# === HELP MODE ===
if ($arg -eq "Help") {
    Write-Host "`n[Photon Build Help]"
    Write-Host "  .\winBuild.ps1           -> Build Debug version and run automatically"
    Write-Host "  .\winBuild.ps1 Release   -> Build Release version and run automatically"
    Write-Host "  .\winBuild.ps1 Clean     -> Remove build artifacts"
    Write-Host ""
    exit 0
}

# === CLEAN MODE ===
if ($arg -eq "Clean") {
    Write-Host "[Clean] Cleaning build artifacts..."
    Remove-Item -Recurse -Force .cache, artifacts, Photon.exe -ErrorAction SilentlyContinue
    Write-Host "[Clean] Clean complete."
    exit 0
}

# === BUILD CONFIG ===
$BUILD_TYPE = $arg
$PROJECT_NAME = "Photon"
$BUILD_DIR = "artifacts"
$VULKAN_SDK_PATH = $env:VULKAN_SDK
$FFMPEG_BIN_PATH = "C:\ffmpeg\bin"

# Vulkan debug environment
$env:VK_INSTANCE_LAYERS = "VK_LAYER_KHRONOS_validation"
$env:VK_DEBUG = "1"

# === SETUP ===
Write-Host "`n[Setup] Starting $BUILD_TYPE build for $PROJECT_NAME..."

if (!(Test-Path $BUILD_DIR)) {
    Write-Host "[Setup] Creating build directory..."
    New-Item -ItemType Directory -Path $BUILD_DIR | Out-Null
} else {
    Write-Host "[Info] Using existing build directory: $BUILD_DIR"
}

# Move into build directory
Push-Location $BUILD_DIR

# === CONFIGURE CMAKE ===
Write-Host "[CMake] Configuring..."
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DVulkan_LIBRARY="$VULKAN_SDK_PATH\Lib\vulkan-1.lib" ..

if ($LASTEXITCODE -ne 0) {
    Write-Host "[Error] CMake configuration failed."
    Pop-Location
    exit 1
}

# === BUILD PROJECT (includes shaders) ===
Write-Host "[Build] Building project (C++ + shaders)..."
cmake --build . --config $BUILD_TYPE --parallel

if ($LASTEXITCODE -ne 0) {
    Write-Host "[Error] Build failed."
    Pop-Location
    exit 1
}

# === DLL COPY ===
$TARGET_DIR = ".\photon\$BUILD_TYPE"
$FULL_TARGET_PATH = Join-Path (Get-Location) $TARGET_DIR
Write-Host "[Copy] Copying FFmpeg DLLs to $FULL_TARGET_PATH..."

if (Test-Path $FFMPEG_BIN_PATH) {
    Get-ChildItem -Path $FFMPEG_BIN_PATH -Filter *.dll | ForEach-Object {
        Copy-Item -Force $_.FullName $FULL_TARGET_PATH
    }
    Write-Host "[Copy] DLLs copied."
} else {
    Write-Host "[Warning] FFmpeg bin folder not found at $FFMPEG_BIN_PATH"
}

Pop-Location

# === EXECUTABLE PATH ===
$EXE_PATH = Join-Path $PWD "$BUILD_DIR\photon\$BUILD_TYPE\$PROJECT_NAME.exe"

if (Test-Path $EXE_PATH) {
    Write-Host "`n[Launch] Launching $PROJECT_NAME..."
    Copy-Item -Force $EXE_PATH ".\Photon.exe"
    $env:PATH = "$PWD\$BUILD_DIR\photon\$BUILD_TYPE;$env:PATH"
    Start-Process ".\Photon.exe"
    Write-Host "[Success] Build and launch complete."
} else {
    Write-Host "[Error] Executable not found: $EXE_PATH"
}
