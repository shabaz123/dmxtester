# MS Windows PowerShell Build Script for Pi Pico
# rev 1 - june 2026 - shabaz

param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

# Use paths matching blink project
$env:CC  = "C:/DEV/vhd_mounts/msys2/msys64/mingw64/bin/gcc.exe"
$env:CXX = "C:/DEV/vhd_mounts/msys2/msys64/mingw64/bin/g++.exe"

$env:Path =
    "C:\DEV\vhd_mounts\msys2\msys64\mingw64\bin;" +
    "C:\DEV\arm_gnu_toolchains\15.2.rel1\bin;" +
    "C:\DEV\tools\bin;" +
    "C:\DEV\tools\cmake\bin;" +
    $env:Path

$buildDir = "build-$Config"

Write-Host ""
Write-Host "========================================"
Write-Host " Building $Config configuration"
Write-Host "========================================"
Write-Host ""
Write-Host "Build directory: $buildDir"
Write-Host ""

Write-Host "ARM Compiler:"
arm-none-eabi-gcc --version | Select-Object -First 1

Write-Host "WIN Compiler for picotool:"
& $env:CC --version | Select-Object -First 1

Write-Host ""

cmake -S . -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE:STRING=$Config"
cmake --build $buildDir
