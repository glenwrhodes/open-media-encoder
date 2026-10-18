# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.6.0] - 2026-10-18

### Added
- Splash screen: shown the moment the app starts while FFmpeg is checked and presets load, with
  the app's name, version, release date and author over new artwork. It stays up for a few
  seconds; click it to close it sooner.
- **GPU everywhere it helps:**
  - Built-in H.264, HEVC and AV1 presets encode on the GPU (NVENC, Quick Sync, AMF, VideoToolbox,
    VA-API) when one works on the machine. Turn this off in Preferences > Encoding.
  - Hardware decoding of sources is on by default (existing settings switch over once).
  - Speech recognition (captions, filler words) on NVIDIA GPUs on Windows with an optional
    download of whisper.cpp's CUDA build (Preferences > Captions; offered once when an NVIDIA GPU is
    found). About 5x faster than the processor on an RTX 4090.
  - Linux packages include a Vulkan build of whisper.cpp for NVIDIA, AMD and Intel GPUs, used when
    the system has a Vulkan driver.
  - The log says when speech recognition ran on the GPU.

### Fixed
- GPU decoding is tested at startup and only used where it works: on Linux without VA-API drivers,
  FFmpeg would otherwise stop with an error instead of decoding in software.

## [1.5.0] - 2026-10-18

### Added
- **Target file size**: choose "Target File Size" as the bitrate mode and the output stays under
  the size you set (for example 10 MB for Discord or 25 MB for email). The bitrate is worked out
  from the length and encoded in two passes; if the file still comes out too big, it is encoded
  again a little lower. New "Fit to File Size" presets for Discord, WhatsApp and email.
- **Target quality (VMAF)**: choose a VMAF score instead of a bitrate (95 looks the same as the
  source to most viewers). Short test encodes spread through the clip find the quality setting
  that reaches it with the smallest file. Works with software and GPU encoders; new "Visually
  Transparent (VMAF 95)" presets for H.264 and HEVC.
- **Compare before and after**: a new Compare tab in Export Settings test-encodes a few seconds
  from the playhead and shows it beside the original with a draggable divider, zoom (100–400%)
  and the test's bitrate, VMAF score, speed and projected file size.
- **Automatic captions** from the speech in the audio, made offline with whisper.cpp (bundled).
  Save an `.srt` next to the output, add a subtitle track (MP4, MOV, WebM) and/or burn the
  captions into the picture, sized for the frame (vertical video included). The language is
  detected automatically or can be set, and captions can be translated to English. Choose a
  Fast, Balanced or Best speech model in Preferences > Captions; it downloads on first use and is
  checked against a pinned SHA-256.
- **Remove silence**: cuts pauses out of the video and audio together, frame-accurately, with a
  silence level, shortest pause and padding around speech you can set. New "Talk & Tutorial -
  Silence Removed" and "Podcast - Silence Removed" presets.
- **Remove filler words** (advanced option of Remove Silence): cuts "um", "uh", "er", "hmm" and any
  words you add. Speech recognition with word timings finds them and the audio gives the exact cut
  points; only fillers said on their own are cut, so no real words are clipped. Uses the same
  speech model as captions. On the command line: `--remove-fillers`.
- **Blurred background** scaling: fit the whole picture into a different frame shape (such as a
  landscape clip as a vertical Short or Reel) over a blurred, darkened copy of itself instead of
  black bars or cropping. New Blurred Background presets for YouTube Shorts, TikTok, Instagram
  Reels, square and 16:9.
- Command line: `--target-size`, `--target-vmaf`, `--size`, `--scale`, `--remove-silence`, `--remove-fillers`,
  `--captions`, `--language` and `--translate` work on top of any preset.
- The Encoding panel and the command line show what an encode is doing (finding pauses, finding
  quality, transcribing, pass 1 of 2), and the log records what each step found.

### Fixed
- CI downloads retry on any error, so a single failed response no longer fails a build.

## [1.4.0] - 2026-10-16

### Added
- **Linux version**, released as an AppImage (download, `chmod +x`, run) and a `.tar.gz` for
  systems without FUSE. FFmpeg 8.0.1 is included, as on Windows. Built on Ubuntu 22.04, so it
  runs on Ubuntu 22.04, Debian 12, Fedora 36 and newer.
- **VA-API** hardware encoding on Linux (H.264, HEVC, AV1) for Intel and AMD GPUs. NVENC and Quick
  Sync also work on Linux.
- **Apple VideoToolbox** hardware encoding (H.264, HEVC) for the coming macOS version.
- Linux and macOS integration: keep the computer awake while encoding or recording (systemd-logind
  / caffeinate), Show in Folder selects the file in the system file manager, and FFmpeg and
  yt-dlp are also found in Homebrew and `/usr/local` when the app is started from a launcher.
- Command-line runs on a Linux server without a display work without extra setup.
- CI builds and tests on Linux and macOS as well as Windows.

### Fixed
- Stream recordings from YouTube with a time limit now stop at the limit; they ran up to
  15 seconds over because picture and sound arrive separately. Recordings also start closer to
  the moment you press Record.
- The Media Browser opens your home folder when there is no Videos folder.

## [1.3.0] - 2026-10-16

### Added
- **Stream Recorder**: paste a link and press Record to save a live stream to disk as it plays.
  - Works with web pages such as **YouTube and Twitch live streams**. The app downloads the free
    yt-dlp helper the first time (after asking), verifies its checksum, and keeps it up to date.
    YouTube live sends picture and sound separately; both are recorded into one file in sync.
  - Also works with direct links: HLS (`.m3u8`), DASH, RTMP, RTSP (IP cameras), SRT, UDP and
    HTTP streams, including internet radio.
  - Recording copies the stream as is: no re-encoding, no quality loss and very little CPU.
  - Save as MP4, MKV or MPEG-TS. MP4 recordings are written as MKV while recording, so nothing is
    lost if the computer crashes, and turned into a normal MP4 at the end.
  - Stop after a set time (15 minutes to 12 hours, or a custom length), or record until stopped.
  - If the stream drops, the recorder reconnects and continues in a new part.
  - Record several streams at once, alongside the encoding queue. Finished recordings can be
    sent to the encoding queue from the right-click menu.
  - File → Record Stream (Ctrl+Shift+R) opens the panel and pastes a link from the clipboard.
- `--record <link>` on the command line, with `--duration`, `--format` and `-o`. Combined with
  Windows Task Scheduler this gives scheduled recordings.
- Preferences → Recording: reconnect option and yt-dlp location, version and update button.

### Changed
- The computer is kept awake while recording as well as while encoding, and "When Queue
  Finishes" sleep or shut down waits for running recordings to end.

## [1.2.0] - 2026-10-16

### Added
- **Web & Social presets**, grouped by platform in the Preset Browser: YouTube (4K, 1440p,
  1080p, 720p, 480p, Shorts, plus a 4K HEVC option), Vimeo, TikTok, Instagram (Reels, Stories,
  portrait, square and landscape feed), Facebook, X (Twitter), LinkedIn, Snapchat, Pinterest and
  generic vertical/square/portrait. They follow each platform's recommended settings, cap the
  frame rate at 60 fps, and raise the bitrate automatically for 50/60 fps sources. Vertical and
  square presets crop to fill.
- Dragging a preset onto a newly added source **replaces its default output** instead of adding a
  second one: drag in a movie, drop "YouTube 720p HD" on it, done.
- **Image sequence input**: File → Add Image Sequence (Ctrl+Shift+I), right-click a frame in the
  Media Browser, or select several numbered frames. The whole sequence is detected from any frame
  (padded or unpadded numbering, any start number), you pick the frame rate, and it can be
  converted to any format. Alpha is preserved, e.g. into ProRes 4444.
  The command line gains `--sequence` and `--fps`.
- Export Settings timeline: a scrubber with In/Out tick marks and the selected range highlighted.
  The **Output** tab scrubs only the In–Out range that will be encoded. I/O keys set In/Out; arrow
  keys step frames (Shift: seconds).
- Video settings: **Maximum Frame Rate** and **raise bitrate above 30 fps** options.
- AAC 384 kbps.

### Changed
- Set In / Set Out and Crop are now only on the Source tab of Export Settings; the Output tab
  shows the range being encoded.

### Fixed
- Output names for sources whose path doesn't exist on disk (such as image sequence patterns)
  were wrongly treated as clashing with the source.

## [1.1.0] - 2026-10-16

### Added
- **Media Browser file list rebuilt**: click any column to sort; a new **Type** column groups
  files by extension (all MP4s together, then WAVs, …); right-click the header to show or hide
  optional columns: Kind, Duration, Resolution, Frame Rate, Codec, Audio, Bitrate and Date
  Created. Media columns are read in the background and cached. Column layout and sort order
  are remembered.
- Media Browser filter box and Video / Audio / Images filter, sub-folders in the list
  (double-click to open), Up / Refresh buttons, favorite folders with quick places, path
  auto-completion, and keyboard navigation (Enter, Backspace).
- Media Browser right-click menu: add with a specific preset, stitch the selection, open in the
  default player, reveal in Explorer, copy path, add folder as a watch folder or favorite.
- Overall queue progress and time remaining in the status bar, window title and Windows taskbar button.
- Keeps the computer awake while encoding (can be turned off in Preferences).
- **File → When Queue Finishes**: sleep or shut down after the queue, with a 60-second cancel countdown.
- Reorder sources to set encoding priority (right-click → Move Source, or Ctrl+Up / Ctrl+Down).
- **Play Output** for finished items.

### Fixed
- The folder tree now scrolls to the current folder when the app starts.

## [1.0.0] - 2026-10-16

First public release.

### Added
- Batch encode queue with multiple outputs per source, inline format and preset selection,
  drag and drop, start/pause/resume/stop, parallel encoding and session restore.
- Export Settings dialog with live source/output preview, in/out range, crop, scaling,
  frame-rate conversion, field order, full codec controls (CBR, VBR 1/2-pass, constant quality)
  and estimated file size.
- Effects: 3D LUTs, image overlay, text overlay, timecode burn-in, video limiter, loudness
  normalization (EBU R128 / ATSC A/85 / streaming / podcast) and burned-in captions.
- Formats: H.264, HEVC, AV1, ProRes, DNxHR, CineForm, QuickTime Animation, MXF OP1a, MPEG-2,
  WebM, AVI, Animated GIF, AAC, MP3, WAV, AIFF, FLAC and image sequences.
- Hardware encoding with NVIDIA NVENC, Intel Quick Sync and AMD AMF, tested on the machine
  at startup.
- About 90 built-in presets, plus user presets with import and export.
- Watch folders, clip stitching, still-image-to-video, media browser with metadata, encoding
  and error logs.
- Command-line interface for headless encoding.
- Windows installer and portable zip with FFmpeg 8.0.1 bundled.

[Unreleased]: https://github.com/glenwrhodes/open-media-encoder/compare/v1.6.0...HEAD
[1.6.0]: https://github.com/glenwrhodes/open-media-encoder/compare/v1.5.0...v1.6.0
[1.5.0]: https://github.com/glenwrhodes/open-media-encoder/compare/v1.4.0...v1.5.0
[1.4.0]: https://github.com/glenwrhodes/open-media-encoder/compare/v1.3.0...v1.4.0
[1.3.0]: https://github.com/glenwrhodes/open-media-encoder/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/glenwrhodes/open-media-encoder/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/glenwrhodes/open-media-encoder/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/glenwrhodes/open-media-encoder/releases/tag/v1.0.0
