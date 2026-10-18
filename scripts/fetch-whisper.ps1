<#
.SYNOPSIS
    Downloads the pinned whisper.cpp build used for automatic captions (speech to text).

.DESCRIPTION
    Fetches the official Windows x64 CPU build of whisper.cpp, verifies its SHA-256 and copies
    whisper-cli.exe with the DLLs it needs into third_party/whisper. whisper.cpp is built with
    MSVC, so the Visual C++ runtime DLLs it uses are copied in as well (app-local deployment,
    allowed by the Visual C++ redistributable license): the newest copy found in a Visual Studio
    "Redist" folder or System32. CMake puts the folder next to the built executable as whisper\.

    The speech models are not part of this: the app downloads the one the user picks on first use.
#>
param(
    [string]$Destination = (Join-Path $PSScriptRoot "..\third_party\whisper"),
    [switch]$Force
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$Tag = "b5454"
$Sha256 = "6BA69E3482D7826214F90A6A9C84CA07782AEC1E1D0C6A7C30C994FD5D816CCB"
$Url = "https://github.com/ggml-org/whisper.cpp/releases/download/$Tag/whisper-bin-x64.zip"
$LicenseUrl = "https://raw.githubusercontent.com/ggml-org/whisper.cpp/$Tag/LICENSE"
$Runtime = "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll", "vcomp140.dll"

$Destination = [IO.Path]::GetFullPath($Destination)
$stamp = Join-Path $Destination "VERSION"
if (-not $Force -and (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $Tag) -and
    (Test-Path (Join-Path $Destination "whisper-cli.exe"))) {
    Write-Host "whisper.cpp $Tag already present in $Destination"
    exit 0
}

function Get-WithRetry([string]$Uri, [string]$OutFile) {
    for ($attempt = 1; ; $attempt++) {
        try { Invoke-WebRequest -Uri $Uri -OutFile $OutFile -UseBasicParsing; return }
        catch {
            if ($attempt -ge 5) { throw }
            Write-Host "Download failed ($($_.Exception.Message)); retrying in 5 s ..."
            Start-Sleep -Seconds 5
        }
    }
}

$work = Join-Path ([IO.Path]::GetTempPath()) ("ome-whisper-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $work | Out-Null
try {
    $zip = Join-Path $work "whisper.zip"
    Write-Host "Downloading whisper.cpp $Tag ..."
    Get-WithRetry $Url $zip
    $stream = [IO.File]::OpenRead($zip)
    try { $actual = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($stream)).Replace("-", "") }
    finally { $stream.Dispose() }
    if ($actual -ne $Sha256) { throw "SHA-256 mismatch for $Url`n  expected $Sha256`n  actual   $actual" }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $x = Join-Path $work "x"
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $x)
    $bin = Get-ChildItem $x -Recurse -Filter "whisper-cli.exe" | Select-Object -First 1
    if (-not $bin) { throw "whisper-cli.exe not found in $Url" }

    if (Test-Path $Destination) { Remove-Item $Destination -Recurse -Force }
    New-Item -ItemType Directory -Force $Destination | Out-Null
    # whisper-cli and its libraries; ggml picks the fastest ggml-cpu-*.dll for the CPU at run time.
    Get-ChildItem $bin.DirectoryName | Where-Object {
        $_.Name -eq "whisper-cli.exe" -or $_.Name -eq "whisper.dll" -or $_.Name -like "ggml*.dll"
    } | Copy-Item -Destination $Destination

    # Visual C++ runtime: the newest version available on this machine.
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $dirs = @("$env:SystemRoot\System32")
    if (Test-Path $vswhere) {
        foreach ($vs in & $vswhere -all -products * -property installationPath) {
            $dirs += Get-ChildItem "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT", "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC*.OpenMP" `
                -Directory -ErrorAction SilentlyContinue | ForEach-Object FullName
        }
    }
    foreach ($dll in $Runtime) {
        $best = $dirs | ForEach-Object { Join-Path $_ $dll } | Where-Object { Test-Path $_ } |
            Sort-Object { [version]([Diagnostics.FileVersionInfo]::GetVersionInfo($_).FileVersion -replace '[^0-9.].*$', '') } |
            Select-Object -Last 1
        if (-not $best) { throw "$dll (Visual C++ runtime) not found; install the Visual C++ Redistributable" }
        Copy-Item $best $Destination
        Write-Host ("  {0} {1}" -f $dll, [Diagnostics.FileVersionInfo]::GetVersionInfo($best).FileVersion)
    }

    Get-WithRetry $LicenseUrl (Join-Path $Destination "LICENSE.txt")
    Set-Content -Path (Join-Path $Destination "README.txt") -Value @"
whisper.cpp $Tag (MIT License), official Windows x64 CPU build
Release: https://github.com/ggml-org/whisper.cpp/releases/tag/$Tag
Archive: whisper-bin-x64.zip (SHA-256 $($Sha256.ToLower()))
Source:  https://github.com/ggml-org/whisper.cpp/tree/$Tag
The msvcp140/vcruntime140/vcomp140 DLLs are the Microsoft Visual C++ runtime, redistributed app-locally.
"@

    # Smoke test with the copied runtime (DLLs next to the exe are found first).
    $out = & (Join-Path $Destination "whisper-cli.exe") --help 2>&1 | Out-String
    if ($out -notmatch "usage:") { throw "whisper-cli doesn't run:`n$out" }
    Set-Content -Path $stamp -Value $Tag
    Write-Host "whisper.cpp $Tag installed to $Destination"
}
finally {
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
