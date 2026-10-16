# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/glenwrhodes/open-media-encoder/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/glenwrhodes/open-media-encoder/releases/tag/v1.0.0
