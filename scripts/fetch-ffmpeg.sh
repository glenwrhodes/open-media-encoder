#!/usr/bin/env bash
# Downloads the pinned FFmpeg build that ships with Open Media Encoder on Linux and macOS.
#
#   Linux:  static GPL build of FFmpeg 8.0.1 from BtbN/FFmpeg-Builds (a monthly build, which that
#           project keeps long-term)          -> third_party/ffmpeg-linux
#   macOS:  static GPL build of FFmpeg 8.0.1 by Martin Riedl (ffmpeg.martin-riedl.de), signed
#           and notarized, with VideoToolbox  -> third_party/ffmpeg-macos
#
# Every download is checked against the SHA-256 below. CMake copies ffmpeg and ffprobe next to the
# built executable (into the .app bundle on macOS) and into packages.
#
# To move to a newer FFmpeg, update the pins below and run the test suite. Keep the version in step
# with scripts/fetch-ffmpeg.ps1 (Windows).
#
# Usage: scripts/fetch-ffmpeg.sh [destination] [--force]
set -euo pipefail

VERSION="8.0.1"

# Linux (BtbN)
LINUX_TAG="autobuild-2026-02-28-12-59"
LINUX_NAME="ffmpeg-n8.0.1-66-g27b8d1a017"
LINUX_SHA256_X86_64="26d2ecd0cefba99f9d1ea92252a3a4f743a58df4c1c3cf9f8bbaaa40b1fca5b0"
LINUX_SHA256_ARM64="d72cc324cb65dd92a9ffe699cc95ccbacfef4def8d3ad9fe2b5164a6a71b9595"

# macOS (Martin Riedl): separate zips for ffmpeg and ffprobe
MAC_BASE="https://ffmpeg.martin-riedl.de/download/macos"
MAC_ARM64_BUILD="arm64/1766430132_8.0.1"
MAC_ARM64_FFMPEG_SHA256="c56f4e2b2ce26a61becf890d8da3415347a1d7d4418cb514915f21612358b790"
MAC_ARM64_FFPROBE_SHA256="ad74fb26500f9a67864611856a655636d910b47a21c9ee2f2f7bc7cbaa45bbb2"
MAC_X86_64_BUILD="amd64/1766437297_8.0.1"
MAC_X86_64_FFMPEG_SHA256="a6c41c69e829697e408308f1ecd6acdfd0d0a84973ff3a6bf782beba83885ed6"
MAC_X86_64_FFPROBE_SHA256="a09b01e8f54ccdbeb661d48952f412edb858860b38ec7fd1c3901a645e3a3b04"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
os="$(uname -s)"
machine="$(uname -m)"
case "$os" in
    Linux) dest="${root}/third_party/ffmpeg-linux" ;;
    Darwin) dest="${root}/third_party/ffmpeg-macos" ;;
    *) echo "Unsupported system: ${os}" >&2; exit 1 ;;
esac
force=0
for arg in "$@"; do
    case "$arg" in
        --force) force=1 ;;
        *) dest="$arg" ;;
    esac
done

stamp="${dest}/VERSION"
if [[ $force -eq 0 && -f "$stamp" && "$(cat "$stamp")" == "${VERSION}-${os}-${machine}" && -x "${dest}/ffmpeg" &&
      -x "${dest}/ffprobe" ]]; then
    echo "FFmpeg ${VERSION} already present in ${dest}"
    exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

sha256() {
    if command -v sha256sum > /dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
download() { # url file expected-sha256
    curl -fsSL --retry 5 --retry-delay 5 --retry-all-errors -o "$2" "$1"
    local actual
    actual="$(sha256 "$2")"
    if [[ "$actual" != "$3" ]]; then
        echo "SHA-256 mismatch for $1" >&2
        echo "  expected $3" >&2
        echo "  actual   ${actual}" >&2
        exit 1
    fi
}

echo "Downloading FFmpeg ${VERSION} (${os} ${machine}) ..."
mkdir -p "$dest"
if [[ "$os" == "Linux" ]]; then
    case "$machine" in
        x86_64 | amd64) arch="linux64"; sha="$LINUX_SHA256_X86_64" ;;
        aarch64 | arm64) arch="linuxarm64"; sha="$LINUX_SHA256_ARM64" ;;
        *) echo "No pinned FFmpeg build for ${machine}" >&2; exit 1 ;;
    esac
    archive="${LINUX_NAME}-${arch}-gpl-8.0.tar.xz"
    url="https://github.com/BtbN/FFmpeg-Builds/releases/download/${LINUX_TAG}/${archive}"
    download "$url" "${work}/${archive}" "$sha"
    tar -xJf "${work}/${archive}" -C "$work"
    src="${work}/${LINUX_NAME}-${arch}-gpl-8.0"
    install -m 755 "${src}/bin/ffmpeg" "${src}/bin/ffprobe" "$dest/"
    cp "${src}/LICENSE.txt" "${dest}/LICENSE.txt"
    cat > "${dest}/README.txt" << EOF
FFmpeg ${VERSION}, static GPL build by BtbN/FFmpeg-Builds
Release: https://github.com/BtbN/FFmpeg-Builds/releases/tag/${LINUX_TAG}
Archive: ${archive} (SHA-256 ${sha})
Source:  https://ffmpeg.org/releases/ffmpeg-${VERSION}.tar.xz and https://github.com/BtbN/FFmpeg-Builds
EOF
else
    case "$machine" in
        arm64) build="$MAC_ARM64_BUILD"; sha_ffmpeg="$MAC_ARM64_FFMPEG_SHA256"; sha_ffprobe="$MAC_ARM64_FFPROBE_SHA256" ;;
        x86_64) build="$MAC_X86_64_BUILD"; sha_ffmpeg="$MAC_X86_64_FFMPEG_SHA256"; sha_ffprobe="$MAC_X86_64_FFPROBE_SHA256" ;;
        *) echo "No pinned FFmpeg build for ${machine}" >&2; exit 1 ;;
    esac
    download "${MAC_BASE}/${build}/ffmpeg.zip" "${work}/ffmpeg.zip" "$sha_ffmpeg"
    download "${MAC_BASE}/${build}/ffprobe.zip" "${work}/ffprobe.zip" "$sha_ffprobe"
    unzip -q -o "${work}/ffmpeg.zip" -d "$work"
    unzip -q -o "${work}/ffprobe.zip" -d "$work"
    install -m 755 "${work}/ffmpeg" "${work}/ffprobe" "$dest/"
    curl -fsSL --retry 5 --retry-delay 5 --retry-all-errors -o "${dest}/LICENSE.txt" "https://raw.githubusercontent.com/FFmpeg/FFmpeg/n${VERSION}/COPYING.GPLv3"
    cat > "${dest}/README.txt" << EOF
FFmpeg ${VERSION}, static GPL build by Martin Riedl (https://ffmpeg.martin-riedl.de)
Build:   ${MAC_BASE}/${build}/ (ffmpeg.zip SHA-256 ${sha_ffmpeg}, ffprobe.zip SHA-256 ${sha_ffprobe})
Source:  https://ffmpeg.org/releases/ffmpeg-${VERSION}.tar.xz; build scripts: https://git.martin-riedl.de/ffmpeg/build-script
EOF
fi
echo "${VERSION}-${os}-${machine}" > "$stamp"
"${dest}/ffmpeg" -hide_banner -version | head -n 1
echo "FFmpeg ${VERSION} ready in ${dest}"
