param(
    [string]$arg = "Debug"
)

# run ".\winBuild.ps1 help" for help
if ($arg -eq "help") {
    Write-Host "[?] run '.\winBuild.ps1' to compile debug version"
    Write-Host "[?] run '.\winBuild.ps1 Release' to compile release version"
    Write-Host "[?] run '.\winBuild.ps1 clean' to remove build artifacts"
    exit 0
}

# run ".\winBuild.ps1 clean" to remove build artifacts
if ($arg -eq "clean") {
    Remove-Item -Recurse -Force .cache -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force artifacts -ErrorAction SilentlyContinue
    Remove-Item -Force Photon.exe -ErrorAction SilentlyContinue
    Write-Host "[!] Cleaned build artifacts"
    exit 0
}

$BUILD_TYPE = $arg

# Cleanup previous binary at root
if (Test-Path Photon.exe) {
    Remove-Item Photon.exe
}

# Create build dir
if (!(Test-Path artifacts)) {
    New-Item -ItemType Directory -Path artifacts | Out-Null
}

Set-Location artifacts

# Configure and build with MSVC (64-bit)
cmake -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_BUILD_TYPE=$BUILD_TYPE `
      -DVULKAN_SDK="$env:VULKAN_SDK" `
      -DVulkan_LIBRARY="$env:VULKAN_SDK\Lib\vulkan-1.lib" ..
cmake --build . --config $BUILD_TYPE --parallel

Set-Location ..

# Path to the built exe (inside Debug/Release)
$BUILT_EXE = ".\artifacts\photon\$BUILD_TYPE\Photon.exe"

if (Test-Path $BUILT_EXE) {
    Copy-Item -Force $BUILT_EXE .\Photon.exe
    & ".\Photon.exe"
}
else {
    Write-Host "[!] Build succeeded but no exe found at $BUILT_EXE"
}