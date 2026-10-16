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

On Linux, packages bundle unmodified static `ffmpeg` and `ffprobe` binaries (version 8.0.1, GPL
build by [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds), release
`autobuild-2026-02-28-12-59`), likewise under the **GNU General Public License v3**, with the license
in `usr/share/doc/open-media-encoder/licenses/`. The archive is pinned by SHA-256 in
[`scripts/fetch-ffmpeg.sh`](scripts/fetch-ffmpeg.sh); build scripts and sources are linked from
that release.

You can replace the bundled FFmpeg with any other build under *Preferences → FFmpeg*.

## yt-dlp (downloaded on demand, not bundled)

To record from web pages such as YouTube and Twitch, Open Media Encoder downloads
[yt-dlp](https://github.com/yt-dlp/yt-dlp) from the project's official GitHub releases the first
time it is needed, after asking. The download is checked against the release's published SHA-256
list, saved to `%LOCALAPPDATA%\OpenMediaEncoder\Tools`, and run as a separate program. yt-dlp
is released into the public domain under the [Unlicense](https://github.com/yt-dlp/yt-dlp/blob/master/LICENSE);
its Windows build bundles third-party components described in that project. You can point
*Preferences → Recording* at your own copy instead.

## Qt

Open Media Encoder is built with [Qt 6](https://www.qt.io/), used under the
**GNU Lesser General Public License v3**. Qt is linked dynamically: the Qt DLLs in the
package can be replaced with your own compatible builds. Qt source is available from
<https://download.qt.io/official_releases/qt/>.

## Linux AppImage libraries

The Linux AppImage and `.tar.gz` also carry the Qt libraries and plugins (LGPL v3, as above), the
ICU libraries that Qt uses (Unicode license) and a few system libraries from Ubuntu 22.04 that
are not guaranteed on every distribution (for example libdbus, libxcb-cursor and libpcre), each
under its own open-source license (LGPL, MIT/X11 or BSD-style). They are unmodified; their
copyright files are included where available, and sources are available from the respective
projects and from Ubuntu.

## MinGW-w64 runtime

`libgcc_s_seh-1.dll`, `libstdc++-6.dll` and `libwinpthread-1.dll` come from the MinGW-w64 GCC
toolchain. They are licensed under the GPL with the GCC Runtime Library Exception, and the
winpthreads library under a permissive MIT/BSD-style license.
