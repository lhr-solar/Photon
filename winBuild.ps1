param(
    [string]$arg = "Debug"
)

# run ".\winBuild.ps1 help" for help
if ($arg -eq "help") {
    Write-Host "[?] run '.\winBuild.ps1' to compile debug version"
    Write-Host "[?] run '.\winBuild.ps1 Release' to compile release version"
    Write-Host "[?] run '.\winBuild.ps1 dash' to compile dashboard-only (no network/synth)"
    Write-Host "[?] run '.\winBuild.ps1 clean' to remove build artifacts"
    exit 0
}

# run ".\winBuild.ps1 clean" to remove build artifacts
if ($arg -eq "clean") {
    Remove-Item -Recurse -Force .cache -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force artifacts -ErrorAction SilentlyContinue
    Remove-Item -Force Photon.exe -ErrorAction SilentlyContinue
    Remove-Item -Force DashboardOnly.exe -ErrorAction SilentlyContinue
    Write-Host "[!] Cleaned build artifacts"
    exit 0
}

# Dashboard-only build
$DASH_ONLY = $false
if ($arg -eq "dash") {
    $DASH_ONLY = $true
    $BUILD_TYPE = "Debug"
} else {
    $BUILD_TYPE = $arg
}

$env:VK_INSTANCE_LAYERS = "VK_LAYER_KHRONOS_validation"
$env:VK_DEBUG = "1"

# Cleanup previous binary at root
if (Test-Path Photon.exe) {
    Remove-Item Photon.exe
}

# Create build dir
if (!(Test-Path artifacts)) {
    New-Item -ItemType Directory -Path artifacts | Out-Null
}

Set-Location artifacts

# Find and load VS developer environment (supports VS 2025/2022)
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -property installationPath
    $devShell = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    if (Test-Path $devShell) {
        Import-Module $devShell
        Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64" | Out-Null
    }
}

# Add Vulkan SDK tools to PATH (for glslangValidator)
if ($env:VULKAN_SDK) {
    $env:PATH += ";$env:VULKAN_SDK\Bin"
}

# Configure with NMake (works with any VS version)
cmake -G "NMake Makefiles" `
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" `
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON `
      -DVulkan_LIBRARY="$env:VULKAN_SDK/Lib/vulkan-1.lib" ..

if ($DASH_ONLY) {
    cmake --build . --target DashboardOnly
} else {
    cmake --build .
}

Set-Location ..

if ($DASH_ONLY) {
    $BUILT_EXE = ".\artifacts\photon\DashboardOnly.exe"
    if (Test-Path $BUILT_EXE) {
        Copy-Item -Force $BUILT_EXE .\DashboardOnly.exe
        & ".\DashboardOnly.exe"
    } else {
        Write-Host "[!] Build succeeded but no exe found at $BUILT_EXE"
    }
} else {
    $BUILT_EXE = ".\artifacts\photon\Photon.exe"
    if (Test-Path $BUILT_EXE) {
        Copy-Item -Force $BUILT_EXE .\Photon.exe
        & ".\Photon.exe"
    } else {
        Write-Host "[!] Build succeeded but no exe found at $BUILT_EXE"
    }
}
