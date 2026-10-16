<#
.SYNOPSIS
    One-step local build of Open Media Encoder with the Qt MinGW kit.

.EXAMPLE
    .\build.ps1                 # build into .\build (FFmpeg is downloaded on first run)
    .\build.ps1 -Test           # build and run the integration tests
    .\build.ps1 -Package        # build and create dist\ packages (zip; add -Installer for setup.exe)
#>
param(
    [string]$Config = "Release",
    [string]$QtDir = "C:\Qt\6.10.2\mingw_64",
    [string]$ToolsDir = "C:\Qt\Tools",
    [switch]$Test,
    [switch]$Package,
    [switch]$Installer
)
$ErrorActionPreference = "Stop"
$env:PATH = "$ToolsDir\mingw1310_64\bin;$ToolsDir\CMake_64\bin;$ToolsDir\Ninja;$QtDir\bin;$env:PATH"
$env:QT_ROOT_DIR = $QtDir
$build = Join-Path $PSScriptRoot "build"

& (Join-Path $PSScriptRoot "scripts\fetch-ffmpeg.ps1")

cmake -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_PREFIX_PATH=$QtDir"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build --parallel
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# Make build\OpenMediaEncoder.exe runnable in place.
& "$QtDir\bin\windeployqt.exe" --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $build "OpenMediaEncoder.exe") | Out-Null

if ($Test) {
    ctest --test-dir $build --output-on-failure
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
if ($Package -or $Installer) {
    & (Join-Path $PSScriptRoot "scripts\package.ps1") -BuildDir $build -QtDir $QtDir -Installer:$Installer
}
