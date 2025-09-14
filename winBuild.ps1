param(
    [string]$arg = "Debug"
)

# run ".\build.ps1 help" for help
if ($arg -eq "help") {
    Write-Host "[?] run '.\winBuild.ps1' to compile debug version"
    Write-Host "[?] run '.\winBuild.ps1 Release' to compile release version"
    Write-Host "[?] run '.\winBuild.ps1 clean' to remove build artifacts"
    exit 0
}

# run ".\build.ps1 clean" to remove build artifacts
if ($arg -eq "clean") {
    Remove-Item -Recurse -Force .cache -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force artifacts -ErrorAction SilentlyContinue
    Remove-Item -Force Photon.exe -ErrorAction SilentlyContinue
    Write-Host "[!] Cleaned build artifacts"
    exit 0
}

$BUILD_TYPE = $arg

# Cleanup previous binary
if (Test-Path Photon.exe) {
    Remove-Item Photon.exe
}

# Create build dir
if (!(Test-Path artifacts)) {
    New-Item -ItemType Directory -Path artifacts | Out-Null
}

Set-Location artifacts

# Configure CMake (adjust compiler paths if using MSVC instead of MinGW)
#cmake -G Ninja -DCMAKE_BUILD_TYPE=$BUILD_TYPE ..
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVULKAN_SDK="$env:VULKAN_SDK" -DVulkan_LIBRARY="$env:VULKAN_SDK\Lib\vulkan-1.lib" ..
ninja -j $env:NUMBER_OF_PROCESSORS

Set-Location ..

# Move binary up
if (Test-Path ".\artifacts\photon\Photon.exe") {
    Move-Item -Force .\artifacts\photon\Photon.exe .\Photon.exe
}

# Run program
if (Test-Path ".\Photon.exe") {
    .\Photon.exe
}