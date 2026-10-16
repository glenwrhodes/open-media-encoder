<p align="center">
  <img src="docs/images/logo.png" width="112" alt="Open Media Encoder logo">
</p>

<h1 align="center">Open Media Encoder</h1>

<p align="center">
  <strong>A free, open-source batch media encoder for Windows and Linux, built on Qt 6 and FFmpeg.</strong><br>
  Queue up your media, pick presets, add LUTs, overlays and loudness normalization, and encode
  to H.264, HEVC, AV1, ProRes, DNxHR and more, with GPU acceleration.
</p>

<p align="center">
  <a href="https://github.com/glenwrhodes/open-media-encoder/actions/workflows/build.yml"><img alt="Build" src="https://github.com/glenwrhodes/open-media-encoder/actions/workflows/build.yml/badge.svg"></a>
  <a href="https://github.com/glenwrhodes/open-media-encoder/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/glenwrhodes/open-media-encoder?label=download&color=2d8ceb"></a>
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-blue.svg"></a>
  <img alt="Platforms: Windows and Linux" src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey">
  <a href="CONTRIBUTING.md"><img alt="PRs welcome" src="https://img.shields.io/badge/PRs-welcome-brightgreen.svg"></a>
</p>

<p align="center">
  <img src="docs/images/main-window.png" alt="Open Media Encoder main window: preset browser, encode queue with multiple outputs per source, watch folders and live encoding progress">
</p>

---

## Download

**[⬇ Download the latest release](https://github.com/glenwrhodes/open-media-encoder/releases/latest)**

| Package | Use it if… |
| --- | --- |
| `OpenMediaEncoder-x.y.z-win64-setup.exe` | You want a normal install with a Start menu entry and an uninstaller. |
| `OpenMediaEncoder-x.y.z-win64-portable.zip` | You want to unzip and run without installing (USB stick, locked-down PC). |
| `OpenMediaEncoder-x.y.z-x86_64.AppImage` | You're on Linux: `chmod +x` the file and run it. |
| `OpenMediaEncoder-x.y.z-linux-x86_64.tar.gz` | You're on Linux without FUSE (some containers): extract and run `open-media-encoder`. |

Everything is included, **FFmpeg too**, so there is nothing else to install.

- **Windows:** 64-bit Windows 10 or 11. GPU encoding needs NVIDIA driver 570 or newer (NVENC), a
  recent Intel graphics driver (Quick Sync) or an AMD driver (AMF).
- **Linux:** 64-bit x86 with glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36, Linux Mint
  21 or newer). GPU encoding uses VA-API (Intel and AMD with Mesa drivers), NVENC (NVIDIA
  proprietary driver) or Quick Sync. Runs on X11 and on Wayland through XWayland.

The app detects what your machine supports and falls back to software encoding automatically.

> Windows SmartScreen may warn about a new, unsigned app. Choose **More info → Run anyway**.
> Every release lists SHA-256 checksums so you can verify the download.

## Features

**Batch queue**
- Several outputs per source: one clip can become a YouTube upload, a ProRes master and an audio stem in one pass.
- Drag files in from Explorer or the built-in Media Browser; drag presets onto sources.
- Start, pause (suspends the encoder), resume and stop. Run encodes in parallel. The queue is restored on the next launch.
- Stitch clips together into one output, or turn a still image into a video.
- Convert **image sequences** (PNG, EXR, DPX, TIFF, JPEG, …) to any format: pick any frame and the
  whole numbered sequence is detected. Alpha is kept for ProRes 4444 and similar.
- Reorder sources to set priority (Ctrl+Up / Ctrl+Down).
- Overall progress and time remaining appear in the status bar and on the Windows taskbar button.
- The computer stays awake while encoding, and can optionally sleep or shut down when the queue finishes.

**Media Browser**

<img src="docs/images/media-browser.png" alt="Media Browser sorted by file type, with Duration and Resolution columns" width="560">

- Click any column to sort. Sort by **Type** to group every MP4 together, every WAV together, and so on.
- Right-click the header to add columns: Kind, Duration, Resolution, Frame Rate, Codec, Audio,
  Bitrate and Date Created. Media details are read in the background.
- Filter by name or by Video / Audio / Images, keep favorite folders, and browse with Enter and Backspace.
- Right-click files to add them with a specific preset, stitch them, play them or reveal them in Explorer.

**Export settings with a live preview**

<img src="docs/images/export-settings.png" alt="Export Settings dialog with live output preview showing a text overlay and burned-in timecode" width="100%">

- Source/Output preview rendered through the real filter graph. Set In/Out (I/O keys) and crop on
  the Source tab; the Output tab scrubs exactly the range that will be encoded.
- Frame size with fit, fill or stretch scaling; frame-rate conversion (sampling, blending, optical flow); field order and deinterlacing.
- Codec controls: speed preset, profile, level, pixel format, CBR / VBR 1-pass / VBR 2-pass / constant quality, keyframe distance.
- Audio codec, sample rate, channels and bitrate, plus an estimated file size and the exact FFmpeg command it will run.

**Effects**
- 3D LUTs (`.cube`, `.3dl`), image overlay / watermark, text overlay, timecode burn-in.
- Broadcast-safe video limiter.
- Loudness normalization to EBU R128, ATSC A/85, streaming or podcast targets.
- Burned-in captions from `.srt`, `.ass` or `.vtt`.

**Formats and presets**

| Category | Formats |
| --- | --- |
| Delivery | H.264, HEVC (H.265), AV1, VP9/VP8 (WebM), MPEG-2, AVI, Animated GIF |
| Mastering | Apple ProRes 422 Proxy → 4444 XQ (with alpha), Avid DNxHR LB → 444, GoPro CineForm, QuickTime Animation, MXF OP1a |
| GPU encoders | NVIDIA NVENC, Intel Quick Sync, AMD AMF, VA-API on Linux, Apple VideoToolbox (each tested on your machine at startup) |
| Audio | AAC, MP3, WAV, AIFF, FLAC, Opus, AC-3 |
| Stills | PNG, JPEG, TIFF, DPX, BMP and Targa, as single frames or sequences |

**Web & Social presets** for YouTube (4K to 480p, Shorts), Vimeo, TikTok, Instagram (Reels,
Stories, feed), Facebook, X, LinkedIn, Snapchat and Pinterest follow each platform's recommended
settings. Drag a movie into the queue, drop "YouTube 720p HD" on it, and press Start. Vertical
presets crop landscape footage to fill, and bitrates rise automatically for 60 fps sources.

Altogether there are 120+ presets, including ProRes/DNxHR mastering and GPU encoding. You can
save, import and share your own presets as JSON files.

**Stream Recorder**
- Paste a link to a live stream and press **Record**. It saves the stream to disk as it plays.
- **YouTube and Twitch** pages work directly: paste the watch page (or a channel's `/live` link)
  and the stream behind it is found for you. This uses the free
  [yt-dlp](https://github.com/yt-dlp/yt-dlp) helper, which the app downloads on first use and keeps up to date.
- Direct stream links work too: HLS (`.m3u8`), DASH, RTMP, RTSP IP cameras, SRT, UDP and internet radio.
- The stream is copied as is, with no re-encoding or quality loss, into MP4, MKV or MPEG-TS. MP4
  recordings are crash-safe: they're written as MKV and converted when the recording ends.
- Stop after a set time or record until stopped. If the stream drops, it reconnects and continues
  in a new part. Record several streams at once while the queue keeps encoding.
- DRM-protected streams (Netflix and similar) can't be recorded. Only record streams you have the
  right to save, and respect each site's terms.

**Automation**
- **Watch folders:** drop files into a folder and they are encoded automatically into `Output/`, and the source moves to `Source/`.
- **Command line:** headless encoding for scripts and render farms (see below).
- An encoding log and an error log record the full FFmpeg command and output for every job.

## Command line

```powershell
OpenMediaEncoder.exe --list-presets
OpenMediaEncoder.exe --preset "H.264/YouTube 1080p HD" -o out.mp4 input.mov
OpenMediaEncoder.exe --preset "sys/quicktime/Apple ProRes 422 HQ" -o D:\masters clip1.mp4 clip2.mp4
OpenMediaEncoder.exe --preset "HEVC/HD 1080p" --in 10 --out 25 -o excerpt.mp4 input.mov
OpenMediaEncoder.exe --preset "H.264/Match Source - High bitrate" --stitch -o reel.mp4 a.mov b.mov c.mov
OpenMediaEncoder.exe --preset "TikTok 1080x1920" --sequence --fps 30 -o title.mp4 renders\title_0001.png
```

Record a live stream (Ctrl+C stops it and keeps what was recorded):

```powershell
OpenMediaEncoder.exe --record "https://www.youtube.com/@SomeChannel/live" --duration 3600 -o D:\Recordings
OpenMediaEncoder.exe --record "rtsp://192.168.1.20/stream1" --format mkv -o porch-camera.mkv
```

Run `--record` from Windows Task Scheduler to record a stream at a set time.

Run with no `--preset` to open the app with the given files queued. The exit code is `0` on
success and `1` if any encode failed.

## Building from source

### Windows

**Prerequisites:** Windows 10/11 x64, [Qt 6.5+](https://www.qt.io/download-qt-installer) with the
MinGW 64-bit kit (or MSVC 2022), CMake 3.21+ and Ninja. The Qt online installer can install
CMake, Ninja and MinGW for you.

```powershell
git clone https://github.com/glenwrhodes/open-media-encoder.git
cd open-media-encoder
.\build.ps1 -Test                 # downloads the pinned FFmpeg, builds, runs the test suite
.\build\OpenMediaEncoder.exe
```

`build.ps1` assumes Qt is installed at `C:\Qt\6.10.2\mingw_64`; pass `-QtDir` / `-ToolsDir` to
change that. `.\build.ps1 -Package -Installer` produces the same zip and installer as the
release pipeline (the installer needs [Inno Setup 6](https://jrsoftware.org/isinfo.php)).

<details>
<summary>Using CMake directly (e.g. with MSVC)</summary>

```powershell
.\scripts\fetch-ffmpeg.ps1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.10.2\msvc2022_64
cmake --build build
ctest --test-dir build --output-on-failure
```
Run these from a *Developer PowerShell for VS 2022*. Qt Creator also opens `CMakeLists.txt` directly.
</details>

### Linux

**Prerequisites:** Qt 6.5+ (the [Qt online installer](https://www.qt.io/download-qt-installer) or
`pip install aqtinstall && aqt install-qt linux desktop 6.10.2 linux_gcc_64`), CMake 3.21+, Ninja
and a C++17 compiler. On Ubuntu/Debian: `sudo apt install build-essential ninja-build libgl1-mesa-dev libxkbcommon-x11-0 libxcb-cursor0`.

```bash
./scripts/fetch-ffmpeg.sh            # pinned static FFmpeg 8.0.1, verified by SHA-256
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=~/Qt/6.10.2/gcc_64
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
./build/OpenMediaEncoder
./scripts/package-linux.sh build dist   # AppImage and .tar.gz, as released
```

### macOS

The code builds and the test suite passes on macOS (CI checks every change), but there is no
signed package yet. To try it: `brew install qt ninja ffmpeg`, then the same CMake commands as
Linux; the app is `build/Open Media Encoder.app`.

## How it works

```
src/core   UI-free engine: format/codec registry, export settings, ffprobe probing, the FFmpeg
           command builder (filter graphs, rate control, two-pass), presets, queue model,
           encoding engine, watch folders, stream recorder
src/ui     Qt Widgets front end: main window and panels, Export Settings dialog, preferences
tests      Integration tests that run real encodes through FFmpeg for every preset and effect
scripts    FFmpeg fetch (pinned + checksummed) and packaging
installer  Inno Setup script for the Windows installer
```

The core library has no widget dependencies, which keeps it easy to test and to reuse (for
example in a future macOS/Linux front end or a server). See [CONTRIBUTING.md](CONTRIBUTING.md#architecture)
for a guided tour.

## Roadmap

Help is welcome on any of these. See [CONTRIBUTING.md](CONTRIBUTING.md) to get started.

- [x] Linux builds (AppImage)
- [ ] macOS package: signed and notarized `.dmg` (the code already builds and passes tests on macOS)
- [ ] Linux on ARM64 and Flatpak/Flathub packaging
- [ ] HDR: HDR10 / HLG metadata passthrough and tone mapping
- [ ] Audio channel mapping and multi-track audio export
- [ ] Proxy-workflow inputs
- [ ] Scheduled recordings in the app (the command line can already be scheduled)
- [ ] Publish destinations (upload to YouTube, Vimeo, S3, FTP after encoding)
- [ ] Translations (wrap UI strings in `tr()` and add Qt Linguist catalogues)
- [ ] More presets: broadcast delivery specs, archival, platform updates

## Contributing

Contributions of every size are welcome: bug reports, presets, docs, translations and code.

- Read the [contributing guide](CONTRIBUTING.md) for setup, architecture and conventions.
- Browse [open issues](https://github.com/glenwrhodes/open-media-encoder/issues), especially those labelled
  `good first issue`.
- Ask questions and share ideas in [Discussions](https://github.com/glenwrhodes/open-media-encoder/discussions).

Everyone taking part is expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
Security issues: please see [SECURITY.md](SECURITY.md).

## License

Open Media Encoder is [MIT licensed](LICENSE). Release packages bundle FFmpeg (GPLv3) and Qt
(LGPLv3) as separate components; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Open Media Encoder is an independent project. It is not affiliated with or endorsed by Adobe
Inc.; Adobe and Media Encoder are trademarks of Adobe Inc. FFmpeg is a trademark of Fabrice Bellard.
