#!/usr/bin/env bash
# Builds the pinned whisper.cpp command-line tool used for automatic captions (speech to text).
#
# whisper.cpp publishes no command-line binaries for macOS, and its Linux ones need a newer glibc than
# the releases support, so whisper-cli is built here from the pinned source release (a few minutes).
# It is a single static binary: CPU only on Linux (AVX2), Metal-accelerated on Apple silicon.
#
#   Linux: third_party/whisper-linux/whisper-cli     macOS: third_party/whisper-macos/whisper-cli
#
# Needs cmake and a C++ compiler. The speech models aren't part of this: the app downloads the one
# the user picks on first use.
#
# Usage: scripts/fetch-whisper.sh [destination] [--force]
set -euo pipefail

TAG="b5454"
SRC_SHA256="9aa617f410d9c7f0dae952eb2b77eafdcfb310c2d622cca1f83633fd89a213bc"
SRC_URL="https://github.com/ggml-org/whisper.cpp/archive/refs/tags/${TAG}.tar.gz"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
os="$(uname -s)"
machine="$(uname -m)"
case "$os" in
    Linux) dest="${root}/third_party/whisper-linux" ;;
    Darwin) dest="${root}/third_party/whisper-macos" ;;
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
if [[ $force -eq 0 && -f "$stamp" && "$(cat "$stamp")" == "${TAG}-${os}-${machine}" && -x "${dest}/whisper-cli" ]]; then
    echo "whisper.cpp ${TAG} already present in ${dest}"
    exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

sha256() {
    if command -v sha256sum > /dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

echo "Downloading whisper.cpp ${TAG} source ..."
curl -fsSL --retry 5 --retry-delay 5 --retry-all-errors -o "${work}/src.tar.gz" "$SRC_URL"
actual="$(sha256 "${work}/src.tar.gz")"
if [[ "$actual" != "$SRC_SHA256" ]]; then
    echo "SHA-256 mismatch for ${SRC_URL}" >&2
    echo "  expected ${SRC_SHA256}" >&2
    echo "  actual   ${actual}" >&2
    exit 1
fi
tar -xzf "${work}/src.tar.gz" -C "$work"
src="${work}/whisper.cpp-${TAG}"

# Portable build: no -march=native (the binary runs on other machines), no OpenMP runtime to ship.
# With GGML_NATIVE off, x86-64 builds use AVX2/FMA/F16C (Intel Haswell / AMD Excavator and newer).
flags=(-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF
       -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_SERVER=OFF -DWHISPER_SDL2=OFF -DWHISPER_CURL=OFF)
if [[ "$os" == "Darwin" ]]; then
    flags+=(-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0)
fi
echo "Building whisper-cli (${os} ${machine}) ..."
cmake -S "$src" -B "${work}/build" "${flags[@]}" > "${work}/configure.log" || { cat "${work}/configure.log"; exit 1; }
jobs="$(getconf _NPROCESSORS_ONLN 2> /dev/null || sysctl -n hw.ncpu)"
cmake --build "${work}/build" --target whisper-cli --parallel "$jobs" > "${work}/build.log" ||
    { tail -n 60 "${work}/build.log"; exit 1; }

rm -rf "$dest"
mkdir -p "$dest"
install -m 755 "${work}/build/bin/whisper-cli" "$dest/"
cp "${src}/LICENSE" "${dest}/LICENSE.txt"
cat > "${dest}/README.txt" << EOF
whisper.cpp ${TAG} (MIT License), whisper-cli built from source for ${os} ${machine}
Source:  ${SRC_URL} (SHA-256 ${SRC_SHA256})
Options: ${flags[*]}
EOF
"${dest}/whisper-cli" --help 2>&1 | grep -q "usage:" || { echo "whisper-cli doesn't run" >&2; exit 1; }
echo "${TAG}-${os}-${machine}" > "$stamp"
echo "whisper.cpp ${TAG} ready in ${dest}"
