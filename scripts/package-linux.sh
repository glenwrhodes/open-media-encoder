#!/usr/bin/env bash
# Builds the Linux release packages from a finished CMake build:
#   dist/OpenMediaEncoder-<version>-<arch>.AppImage      single file; chmod +x and run
#   dist/OpenMediaEncoder-<version>-linux-<arch>.tar.gz  same contents unpacked (no FUSE needed)
#   dist/SHA256SUMS-linux.txt
#
# Usage: scripts/package-linux.sh [build-dir] [out-dir]
# Needs Qt's qmake on PATH (or QMAKE=/path/to/qmake). The pinned linuxdeploy tools are downloaded
# into <out-dir>/.tools and checked against the SHA-256 values below.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(realpath "${1:-${root}/build}")"
out="$(realpath -m "${2:-${root}/dist}")"
version="$(grep -m1 -oP '^\s+VERSION\s+\K[0-9]+\.[0-9]+\.[0-9]+' "${root}/CMakeLists.txt")"
arch="$(uname -m)"

LINUXDEPLOY_TAG="1-alpha-20251107-1"
QTPLUGIN_TAG="1-alpha-20250213-1"
declare -A SHA=(
    ["linuxdeploy-x86_64.AppImage"]="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
    ["linuxdeploy-aarch64.AppImage"]="620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff"
    ["linuxdeploy-plugin-qt-x86_64.AppImage"]="15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724"
    ["linuxdeploy-plugin-qt-aarch64.AppImage"]="bf1c24aff6d749b5cf423afad6f15abd4440f81dec1aab95706b25f6667cdcf1"
)

tools="${out}/.tools"
mkdir -p "$tools"
fetch() { # repo tag file
    local file="$3" path="${tools}/$3"
    if [[ ! -x "$path" ]]; then
        echo "Downloading $file ..."
        curl -fsSL --retry 5 --retry-delay 5 --retry-all-errors -o "$path" "https://github.com/$1/releases/download/$2/$file"
        chmod +x "$path"
    fi
    local actual
    actual="$(sha256sum "$path" | cut -d' ' -f1)"
    if [[ "$actual" != "${SHA[$file]}" ]]; then
        echo "SHA-256 mismatch for $file: expected ${SHA[$file]}, got $actual" >&2
        rm -f "$path"
        exit 1
    fi
}
fetch linuxdeploy/linuxdeploy "$LINUXDEPLOY_TAG" "linuxdeploy-${arch}.AppImage"
fetch linuxdeploy/linuxdeploy-plugin-qt "$QTPLUGIN_TAG" "linuxdeploy-plugin-qt-${arch}.AppImage"

echo "Packaging Open Media Encoder ${version} (${arch})"
appdir="${out}/AppDir"
rm -rf "$appdir"
cmake --install "$build" --prefix "${appdir}/usr" > /dev/null

export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
# The installed binary has no RPATH, so tell linuxdeploy where Qt's libraries are.
export LD_LIBRARY_PATH="$("$QMAKE" -query QT_INSTALL_LIBS)${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export APPIMAGE_EXTRACT_AND_RUN=1 # containers and CI runners have no FUSE
# X11 (also used on Wayland desktops through XWayland) comes by default; offscreen is for command-line
# use on machines without a display.
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so"
export LDAI_OUTPUT="${out}/OpenMediaEncoder-${version}-${arch}.AppImage"
export OUTPUT="$LDAI_OUTPUT"
rm -f "$LDAI_OUTPUT"
(
    cd "$out"
    "${tools}/linuxdeploy-${arch}.AppImage" --appdir "$appdir" --plugin qt --output appimage \
        --executable "${appdir}/usr/bin/OpenMediaEncoder" \
        --desktop-file "${appdir}/usr/share/applications/open-media-encoder.desktop" \
        --icon-file "${appdir}/usr/share/icons/hicolor/512x512/apps/open-media-encoder.png"
) > "${out}/linuxdeploy.log" 2>&1 || { tail -40 "${out}/linuxdeploy.log"; exit 1; }

# Same files as a plain archive, with a launcher at the top.
tarname="OpenMediaEncoder-${version}-linux-${arch}"
rm -rf "${out:?}/${tarname}"
cp -a "$appdir" "${out}/${tarname}"
cat > "${out}/${tarname}/open-media-encoder" << 'EOF'
#!/bin/sh
exec "$(dirname "$(readlink -f "$0")")/AppRun" "$@"
EOF
chmod +x "${out}/${tarname}/open-media-encoder"
tar -C "$out" -czf "${out}/${tarname}.tar.gz" "$tarname"
rm -rf "${out:?}/${tarname}"

# Smoke test: the packaged app runs on its own and finds its bundled FFmpeg.
count="$(env -u DISPLAY -u WAYLAND_DISPLAY "$LDAI_OUTPUT" --list-presets | wc -l)"
if [[ "$count" -lt 50 ]]; then
    echo "Smoke test failed: only ${count} presets listed" >&2
    exit 1
fi
echo "Package smoke test passed (${count} presets available)"

(cd "$out" && sha256sum "$(basename "$LDAI_OUTPUT")" "${tarname}.tar.gz" > SHA256SUMS-linux.txt)
ls -la "$LDAI_OUTPUT" "${out}/${tarname}.tar.gz"
