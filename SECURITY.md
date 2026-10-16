# Security Policy

## Supported versions

Security fixes are made for the latest release. Please update to the newest version before
reporting an issue.

## Reporting a vulnerability

**Please do not report security vulnerabilities in public issues.**

Report them privately through GitHub:
[**Report a vulnerability**](https://github.com/glenwrhodes/open-media-encoder/security/advisories/new).

Please include:

- A description of the issue and its impact
- Steps to reproduce, or a proof-of-concept (for example a crafted media, preset or queue file)
- The Open Media Encoder version and Windows version

You can expect an acknowledgement within a few days. We will keep you informed while we work on a
fix and credit you in the release notes unless you prefer otherwise.

## Scope notes

- Open Media Encoder runs the bundled FFmpeg to decode and encode media. Vulnerabilities in FFmpeg
  itself should also be reported upstream (<https://ffmpeg.org/security.html>); we will ship
  updated FFmpeg builds in a new release.
- Preset (`.json`) and queue files can contain extra FFmpeg arguments. Only import presets and
  queues from sources you trust, just as you would with scripts.
