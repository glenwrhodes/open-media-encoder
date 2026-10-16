<#
.SYNOPSIS
    Downloads the pinned FFmpeg build that ships with Open Media Encoder.

.DESCRIPTION
    Fetches a static Windows FFmpeg build (gyan.dev "essentials", GPLv3), verifies its SHA-256
    and extracts ffmpeg.exe, ffprobe.exe and the license into third_party/ffmpeg. CMake copies
    these next to the built executable and into release packages, so end users never need to
    install FFmpeg themselves.

    To move to a newer FFmpeg, update $Version and $Sha256 below and run the test suite.
    Note: newer FFmpeg releases can raise the minimum NVIDIA driver for NVENC (FFmpeg 8.1+
    needs driver 610+; 8.0.x needs 570+), so check GPU encoding before bumping.
#>
param(
    [string]$Destination = (Join-Path $PSScriptRoot "..\third_party\ffmpeg"),
    [switch]$Force
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$Version = "8.0.1"
$Sha256 = "E2AAEAA0FDBC397D4794828086424D4AAA2102CEF1FB6874F6FFD29C0B88B673"
$Url = "https://github.com/GyanD/codexffmpeg/releases/download/$Version/ffmpeg-$Version-essentials_build.zip"

$Destination = [IO.Path]::GetFullPath($Destination)
$stamp = Join-Path $Destination "VERSION"
if (-not $Force -and (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $Version) -and
    (Test-Path (Join-Path $Destination "ffmpeg.exe")) -and (Test-Path (Join-Path $Destination "ffprobe.exe"))) {
    Write-Host "FFmpeg $Version already present in $Destination"
    exit 0
}

$work = Join-Path ([IO.Path]::GetTempPath()) ("ome-ffmpeg-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $work | Out-Null
try {
    $zip = Join-Path $work "ffmpeg.zip"
    Write-Host "Downloading FFmpeg $Version ..."
    Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing

    # .NET APIs keep this working in both Windows PowerShell 5.1 and PowerShell 7.
    $stream = [IO.File]::OpenRead($zip)
    try { $actual = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($stream)).Replace("-", "") }
    finally { $stream.Dispose() }
    if ($actual -ne $Sha256) {
        throw "SHA-256 mismatch for $Url`n  expected $Sha256`n  actual   $actual"
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $work)
    $root = Join-Path $work "ffmpeg-$Version-essentials_build"
    New-Item -ItemType Directory -Force $Destination | Out-Null
    Copy-Item (Join-Path $root "bin\ffmpeg.exe") $Destination -Force
    Copy-Item (Join-Path $root "bin\ffprobe.exe") $Destination -Force
    Copy-Item (Join-Path $root "LICENSE") (Join-Path $Destination "LICENSE.txt") -Force
    Copy-Item (Join-Path $root "README.txt") (Join-Path $Destination "README.txt") -Force
    Set-Content -Path $stamp -Value $Version -NoNewline
    Write-Host "FFmpeg $Version installed to $Destination"
}
finally {
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
