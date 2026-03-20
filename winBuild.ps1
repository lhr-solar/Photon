param(
    [string]$arg = "Debug",
    [switch]$EnableValidation
)

# run ".\winBuild.ps1 help" for help
if ($arg -eq "help") {
    Write-Host "[?] run '.\winBuild.ps1' to compile debug version"
    Write-Host "[?] run '.\winBuild.ps1 Release' to compile release version"
    Write-Host "[?] add '-EnableValidation' to enable Vulkan validation layers"
    Write-Host "[?] run '.\winBuild.ps1 clean' to remove build artifacts"
    exit 0
}

# run ".\winBuild.ps1 clean" to remove build artifacts
if ($arg -eq "clean") {
    Remove-Item -Recurse -Force .cache -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force .artifacts -ErrorAction SilentlyContinue
    Remove-Item -Force Photon.exe -ErrorAction SilentlyContinue
    Write-Host "[!] Cleaned build artifacts"
    exit 0
}

$BUILD_TYPE = $arg
$ARTIFACT_ROOT = Join-Path $PSScriptRoot ".artifacts"

if ($EnableValidation) {
    $env:VK_INSTANCE_LAYERS = "VK_LAYER_KHRONOS_validation"
    $env:VK_DEBUG = "1"
    Write-Host "[+] Vulkan validation layers enabled"
}

$clangTool = Get-Command clang-cl -ErrorAction SilentlyContinue
if (-not $clangTool) {
    Write-Error "clang-cl was not found in PATH. Install clang (LLVM) or add it to PATH before running this script."
    exit 1
}

$ninjaTool = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $ninjaTool) {
    Write-Error "ninja was not found in PATH. Install Ninja or add it to PATH before running this script."
    exit 1
}

# Cleanup previous binary at root
if (Test-Path Photon.exe) {
    Remove-Item Photon.exe
}

# Create build dir
if (!(Test-Path $ARTIFACT_ROOT)) {
    New-Item -ItemType Directory -Path $ARTIFACT_ROOT | Out-Null
}

$cachePath = Join-Path $ARTIFACT_ROOT "CMakeCache.txt"
if (Test-Path $cachePath) {
    $generatorLine = Select-String -Path $cachePath -Pattern "CMAKE_GENERATOR:INTERNAL=(.*)" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($generatorLine -and $generatorLine.Matches.Count -gt 0) {
        $previousGenerator = $generatorLine.Matches[0].Groups[1].Value.Trim()
        if ($previousGenerator -ne "Ninja") {
            Write-Host "[!] Previous build directory used generator '$previousGenerator'; resetting for Ninja."
            Remove-Item -Recurse -Force $ARTIFACT_ROOT
            New-Item -ItemType Directory -Path $ARTIFACT_ROOT | Out-Null
        }
    }
}

Push-Location $ARTIFACT_ROOT

$staticReleaseArg = "-DPHOTON_REQUIRE_STATIC_RELEASE=OFF"
if ($BUILD_TYPE -ieq "Release") {
    $staticReleaseArg = "-DPHOTON_REQUIRE_STATIC_RELEASE=ON"
}

# Configure and build with clang-cl + Ninja (64-bit)
cmake -G "Ninja" `
      "-DCMAKE_C_COMPILER=clang-cl" `
      "-DCMAKE_CXX_COMPILER=clang-cl" `
      "-DCMAKE_BUILD_TYPE=$BUILD_TYPE" `
      "$staticReleaseArg" `
      "-DVULKAN_SDK=$($env:VULKAN_SDK)" `
      "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON" `
      "-DVulkan_LIBRARY=$($env:VULKAN_SDK)\Lib\vulkan-1.lib" ..
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    exit $LASTEXITCODE
}

cmake --build . --parallel
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    exit $LASTEXITCODE
}

$compileCommands = Join-Path (Get-Location) "compile_commands.json"
if (Test-Path $compileCommands) {
    Write-Host "[+] Updated artifacts\compile_commands.json for clangd"
}
else {
    Write-Warning "clangd compile_commands.json was not generated"
}

Pop-Location

# Path to the built exe (inside Debug/Release)
$possibleExe = @(
    ".\.artifacts\photon\$BUILD_TYPE\Photon.exe",
    ".\.artifacts\photon\Photon.exe",
    ".\.artifacts\Photon.exe"
)
$BUILT_EXE = $possibleExe | Where-Object { Test-Path $_ } | Select-Object -First 1

if ($BUILT_EXE) {
    Copy-Item -Force $BUILT_EXE .\Photon.exe
    & ".\Photon.exe"
}
else {
    Write-Host "[!] Build succeeded but no exe found at $BUILT_EXE"
}
