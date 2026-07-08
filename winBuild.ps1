# winBuild.ps1 - Configure, build, and run Photon on Windows
#
# Usage (from Photon/ directory):
#   .\winBuild.ps1              # Debug build (default)
#   .\winBuild.ps1 Release      # Release build
#   .\winBuild.ps1 dash         # DashboardOnly target
#   .\winBuild.ps1 clean        # Remove .artifacts and Photon.exe

param(
    [string]$Mode = "Debug"
)

$ErrorActionPreference = "Stop"
$Root      = $PSScriptRoot
$Artifacts = Join-Path $Root ".artifacts"
$VulkanSDK = if ($env:VULKAN_SDK) { $env:VULKAN_SDK } else { "C:\VulkanSDK\1.4.321.1" }

# --- clean -------------------------------------------------------------------
if ($Mode -eq "clean") {
    Write-Host ">> Cleaning build artifacts..." -ForegroundColor Yellow
    if (Test-Path $Artifacts) { Remove-Item -Recurse -Force $Artifacts }
    foreach ($f in @("Photon.exe","DashboardOnly.exe")) {
        $p = Join-Path $Root $f
        if (Test-Path $p) { Remove-Item -Force $p }
    }
    Write-Host ">> Clean done." -ForegroundColor Green
    exit 0
}

# --- resolve build type / target ---------------------------------------------
$BuildType = "Debug"
$Target    = "Photon"

switch ($Mode) {
    "Release" { $BuildType = "Release" }
    "dash"    { $Target    = "DashboardOnly" }
    "Debug"   { }
    default {
        Write-Host "Unknown mode '$Mode'. Use: Debug | Release | dash | clean" -ForegroundColor Red
        exit 1
    }
}

# --- environment -------------------------------------------------------------
$env:VULKAN_SDK         = $VulkanSDK
$env:PATH               = "$VulkanSDK\Bin;$env:PATH"
$env:VK_LAYER_PATH      = "$VulkanSDK\Bin"
$env:VK_INSTANCE_LAYERS = "VK_LAYER_KHRONOS_validation"

Write-Host ">> VULKAN_SDK : $VulkanSDK"  -ForegroundColor DarkGray
Write-Host ">> Build type : $BuildType"  -ForegroundColor DarkGray
Write-Host ">> Target     : $Target"     -ForegroundColor DarkGray

# --- configure (only if CMakeCache missing) ----------------------------------
$CacheFile = Join-Path $Artifacts "CMakeCache.txt"
if (-not (Test-Path $CacheFile)) {
    Write-Host ">> Configuring..." -ForegroundColor Cyan
    $null = New-Item -ItemType Directory -Force -Path $Artifacts
    & cmake -S $Root -B $Artifacts `
            -G Ninja `
            -DCMAKE_BUILD_TYPE=$BuildType `
            -DCMAKE_CXX_COMPILER=clang++ `
            -DCMAKE_C_COMPILER=clang
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: cmake configure failed" -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host ">> CMakeCache found - skipping configure (run 'clean' to force reconfigure)" -ForegroundColor DarkGray
}

# --- build -------------------------------------------------------------------
Write-Host ">> Building $Target [$BuildType]..." -ForegroundColor Cyan
& cmake --build $Artifacts --target $Target --config $BuildType
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: build failed" -ForegroundColor Red
    exit 1
}

# --- locate exe (always in .artifacts/bin/) ----------------------------------
$BinDir   = Join-Path $Artifacts "bin"
$ExeToRun = Join-Path $BinDir "$Target.exe"

if (-not (Test-Path $ExeToRun)) {
    Write-Host "ERROR: $ExeToRun not found after build" -ForegroundColor Red
    exit 1
}

# --- run from bin/ so DLLs (imgui_runtime, photonUI, config.ini) resolve -----
Write-Host ">> Launching $Target from $BinDir ..." -ForegroundColor Green
Set-Location $BinDir
& $ExeToRun
