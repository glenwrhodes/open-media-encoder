<#
.SYNOPSIS
    Creates the distributable Open Media Encoder packages from a finished build.

.DESCRIPTION
    1. Installs the build into a staging folder (exe, bundled FFmpeg, licenses).
    2. Runs windeployqt to add the Qt runtime and compiler runtime.
    3. Produces:
         dist/OpenMediaEncoder-<version>-win64-portable.zip
         dist/OpenMediaEncoder-<version>-win64-setup.exe   (with -Installer; needs Inno Setup 6)
         dist/SHA256SUMS.txt
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [string]$OutDir = (Join-Path $PSScriptRoot "..\dist"),
    [string]$QtDir = $env:QT_ROOT_DIR,
    [switch]$Installer
)
$ErrorActionPreference = "Stop"
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$OutDir = [IO.Path]::GetFullPath($OutDir)

$cmakeLists = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($cmakeLists -notmatch 'VERSION\s+(\d+\.\d+\.\d+)') { throw "Could not read the project version from CMakeLists.txt" }
$Version = $Matches[1]
Write-Host "Packaging Open Media Encoder $Version"

$stage = Join-Path $OutDir "OpenMediaEncoder-$Version"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $OutDir | Out-Null

cmake --install $BuildDir --prefix $stage --config Release
if ($LASTEXITCODE) { throw "cmake --install failed" }
foreach ($f in "OpenMediaEncoder.exe", "ffmpeg.exe", "ffprobe.exe") {
    if (-not (Test-Path (Join-Path $stage $f))) { throw "$f is missing from the package (did you run scripts/fetch-ffmpeg.ps1?)" }
}

$deploy = if ($QtDir) { Join-Path $QtDir "bin\windeployqt.exe" } else { (Get-Command windeployqt.exe -ErrorAction Stop).Source }
& $deploy --release --compiler-runtime --no-translations --no-system-d3d-compiler --no-opengl-sw `
    --skip-plugin-types "tls,networkinformation,qmltooling,generic" (Join-Path $stage "OpenMediaEncoder.exe")
if ($LASTEXITCODE) { throw "windeployqt failed" }

# Smoke test: the packaged app can list presets using only what is inside the package.
$presets = & (Join-Path $stage "OpenMediaEncoder.exe") --list-presets | Out-String
$count = ($presets -split "`n" | Where-Object { $_ -match '^sys/' }).Count
if ($count -lt 40) { throw "Packaged app reported only $count presets; FFmpeg or Qt deployment is broken.`n$presets" }
Write-Host "Package smoke test passed ($count system presets available)"

$zip = Join-Path $OutDir "OpenMediaEncoder-$Version-win64-portable.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
$artifacts = @($zip)

if ($Installer) {
    $iscc = (Get-Command iscc.exe -ErrorAction SilentlyContinue).Source
    if (-not $iscc) {
        $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
                  "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
    }
    if (-not $iscc) { throw "Inno Setup 6 (ISCC.exe) not found" }
    & $iscc "/DAppVersion=$Version" "/DSourceDir=$stage" "/DOutputDir=$OutDir" (Join-Path $root "installer\OpenMediaEncoder.iss")
    if ($LASTEXITCODE) { throw "Inno Setup failed" }
    $artifacts += Join-Path $OutDir "OpenMediaEncoder-$Version-win64-setup.exe"
}

$sums = foreach ($a in $artifacts) { "{0}  {1}" -f (Get-FileHash $a -Algorithm SHA256).Hash.ToLower(), (Split-Path $a -Leaf) }
Set-Content -Path (Join-Path $OutDir "SHA256SUMS.txt") -Value $sums
$artifacts | ForEach-Object { Write-Host ("  {0}  ({1:N1} MB)" -f (Split-Path $_ -Leaf), ((Get-Item $_).Length / 1MB)) }
