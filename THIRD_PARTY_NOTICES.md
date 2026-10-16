# Third-party notices

Open Media Encoder itself is released under the [MIT License](LICENSE). Release packages
also include the third-party components listed below, each under its own license.

## FFmpeg

Release packages bundle unmodified `ffmpeg.exe` and `ffprobe.exe` binaries (version 8.0.1,
"essentials" build by [gyan.dev](https://www.gyan.dev/ffmpeg/builds/)). Open Media Encoder runs
them as separate programs; it does not link against FFmpeg libraries.

- These builds include GPL components (for example x264 and x265), so the bundled binaries are
  distributed under the **GNU General Public License v3**. The full license text ships in
  `licenses/FFmpeg-LICENSE.txt`, and the build configuration and library list in
  `licenses/FFmpeg-BUILD-README.txt`.
- FFmpeg source: <https://ffmpeg.org/releases/ffmpeg-8.0.1.tar.xz>
- Build provenance: <https://github.com/GyanD/codexffmpeg/releases/tag/8.0.1>
- The exact archive is pinned by SHA-256 in [`scripts/fetch-ffmpeg.ps1`](scripts/fetch-ffmpeg.ps1).

You can replace the bundled FFmpeg with any other build under *Preferences → FFmpeg*.

## Qt

Open Media Encoder is built with [Qt 6](https://www.qt.io/), used under the
**GNU Lesser General Public License v3**. Qt is linked dynamically: the Qt DLLs in the
package can be replaced with your own compatible builds. Qt source is available from
<https://download.qt.io/official_releases/qt/>.

## MinGW-w64 runtime

`libgcc_s_seh-1.dll`, `libstdc++-6.dll` and `libwinpthread-1.dll` come from the MinGW-w64 GCC
toolchain. They are licensed under the GPL with the GCC Runtime Library Exception, and the
winpthreads library under a permissive MIT/BSD-style license.
