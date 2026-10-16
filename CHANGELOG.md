# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/glenwrhodes/open-media-encoder/compare/v1.3.0...HEAD
[1.3.0]: https://github.com/glenwrhodes/open-media-encoder/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/glenwrhodes/open-media-encoder/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/glenwrhodes/open-media-encoder/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/glenwrhodes/open-media-encoder/releases/tag/v1.0.0
