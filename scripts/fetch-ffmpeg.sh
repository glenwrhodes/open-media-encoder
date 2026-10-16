#!/usr/bin/env bash
# Downloads the pinned FFmpeg build that ships with Open Media Encoder on Linux.
#
# Fetches a static GPL build of FFmpeg 8.0.1 from BtbN/FFmpeg-Builds (monthly build, which the
# project keeps long-term), verifies its SHA-256 and extracts ffmpeg, ffprobe and the license into
# third_party/ffmpeg-linux. CMake copies them next to the built executable and into packages.
#
# To move to a newer FFmpeg, update TAG, NAME and the checksums below and run the test suite.
# Keep the version in step with scripts/fetch-ffmpeg.ps1 (Windows).
#
# Usage: scripts/fetch-ffmpeg.sh [destination] [--force]
set -euo pipefail

VERSION="8.0.1"
TAG="autobuild-2026-02-28-12-59"
NAME="ffmpeg-n8.0.1-66-g27b8d1a017"
SHA256_X86_64="26d2ecd0cefba99f9d1ea92252a3a4f743a58df4c1c3cf9f8bbaaa40b1fca5b0"
SHA256_ARM64="d72cc324cb65dd92a9ffe699cc95ccbacfef4def8d3ad9fe2b5164a6a71b9595"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${root}/third_party/ffmpeg-linux"
force=0
for arg in "$@"; do
    case "$arg" in
        --force) force=1 ;;
        *) dest="$arg" ;;
    esac
done

case "$(uname -s)" in
    Linux) ;;
    Darwin)
        echo "No pinned macOS FFmpeg yet. For development, 'brew install ffmpeg' works: the app finds it" >&2
        echo "in /opt/homebrew/bin or /usr/local/bin." >&2
        exit 1 ;;
    *) echo "Unsupported system: $(uname -s)" >&2; exit 1 ;;
esac

case "$(uname -m)" in
    x86_64 | amd64) arch="linux64"; sha="$SHA256_X86_64" ;;
    aarch64 | arm64) arch="linuxarm64"; sha="$SHA256_ARM64" ;;
    *) echo "No pinned FFmpeg build for $(uname -m)" >&2; exit 1 ;;
esac

stamp="${dest}/VERSION"
if [[ $force -eq 0 && -f "$stamp" && "$(cat "$stamp")" == "${VERSION}-${arch}" && -x "${dest}/ffmpeg" &&
      -x "${dest}/ffprobe" ]]; then
    echo "FFmpeg ${VERSION} already present in ${dest}"
    exit 0
fi

archive="${NAME}-${arch}-gpl-8.0.tar.xz"
url="https://github.com/BtbN/FFmpeg-Builds/releases/download/${TAG}/${archive}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "Downloading FFmpeg ${VERSION} (${arch}) ..."
curl -fsSL --retry 3 -o "${work}/${archive}" "$url"
actual="$(sha256sum "${work}/${archive}" | cut -d' ' -f1)"
if [[ "$actual" != "$sha" ]]; then
    echo "SHA-256 mismatch for ${url}" >&2
    echo "  expected ${sha}" >&2
    echo "  actual   ${actual}" >&2
    exit 1
fi

tar -xJf "${work}/${archive}" -C "$work"
src="${work}/${NAME}-${arch}-gpl-8.0"
mkdir -p "$dest"
install -m 755 "${src}/bin/ffmpeg" "${src}/bin/ffprobe" "$dest/"
cp "${src}/LICENSE.txt" "${dest}/LICENSE.txt"
cat > "${dest}/README.txt" <<EOF
FFmpeg ${VERSION}, static GPL build by BtbN/FFmpeg-Builds
Release: https://github.com/BtbN/FFmpeg-Builds/releases/tag/${TAG}
Archive: ${archive} (SHA-256 ${sha})
Source:  https://ffmpeg.org/releases/ffmpeg-${VERSION}.tar.xz and https://github.com/BtbN/FFmpeg-Builds
EOF
echo "${VERSION}-${arch}" > "$stamp"
"${dest}/ffmpeg" -hide_banner -version | head -n 1
echo "FFmpeg ${VERSION} ready in ${dest}"
