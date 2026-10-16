# Contributing to Open Media Encoder

Thanks for your interest in improving Open Media Encoder! This guide covers how to get a build
running, how the code is organised, and what we look for in pull requests.

If you're unsure about anything, open a [discussion](https://github.com/glenwrhodes/open-media-encoder/discussions)
or a draft pull request and ask. Questions are always welcome.

## Ways to contribute

- **Report bugs** using the bug report template. Logs make a huge difference: *Help → Show Error Log*
  includes the exact FFmpeg command and its output.
- **Suggest features** using the feature request template, or start a discussion first for big ideas.
- **Add or improve presets.** This is a great first contribution (see [Adding a preset](#adding-a-preset)).
- **Improve docs**, the README or in-app wording.
- **Write code**: pick an issue labelled `good first issue` or `help wanted`, or something from
  the roadmap in the README. For larger changes, please open an issue first so we can agree on
  the approach before you invest time.

## Development setup

You need Windows 10/11 x64 with:

- **Qt 6.5 or newer** (6.10 recommended) with the **MinGW 64-bit** kit, or **MSVC 2022**
- **CMake 3.21+** and **Ninja** (both installable from the Qt online installer)
- **PowerShell 7** (recommended) or Windows PowerShell 5.1

```powershell
git clone https://github.com/<you>/open-media-encoder.git
cd open-media-encoder
.\build.ps1 -Test
```

`build.ps1` downloads the pinned FFmpeg into `third_party/ffmpeg` (verified by SHA-256),
configures CMake, builds, deploys the Qt DLLs into `build\` so the exe runs in place, and runs
the tests. Use `-QtDir` and `-ToolsDir` if Qt isn't in `C:\Qt`.

**Qt Creator:** open `CMakeLists.txt`, pick a Qt 6 kit, and run `scripts\fetch-ffmpeg.ps1` once
so the bundled FFmpeg is copied next to the executable.

**Visual Studio / MSVC:** from a *Developer PowerShell for VS 2022*:

```powershell
.\scripts\fetch-ffmpeg.ps1
cmake -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.10.2\msvc2022_64
cmake --build build-msvc
ctest --test-dir build-msvc --output-on-failure
```

CI builds and tests with both MinGW and MSVC, so please keep both compiling.

## Architecture

```
src/
  core/                     UI-free library (encoder_core), unit-tested
    Formats.*               Registry of output formats and video/audio codecs and their controls
    EncoderCaps.*           Detects encoders/filters in the FFmpeg build; test-encodes GPU encoders
    ExportSettings.*        One output's settings (video, audio, effects) + JSON (de)serialisation
    MediaInfo.*             ffprobe wrapper
    CommandBuilder.*        Turns a BuildRequest into FFmpeg arguments: filter graph (crop, scale,
                            fps, LUT, overlays, drawtext, subtitles, limiter, GIF palette), rate
                            control, two-pass, containers, previews
    PresetManager.*         Built-in system presets + user presets on disk
    QueueModel.*            Qt item model for sources -> outputs; persistence
    EncoderEngine.*         Runs ffmpeg processes, parses -progress, pause/resume, logging
    WatchFolderManager.*    Polls watch folders and reports files once copying has finished
    AppSettings.*           Preferences and well-known paths
  ui/                       Qt Widgets front end
    MainWindow.*            Docks, menus, wiring between panels, engine and model
    QueuePanel.*            Queue view and delegate (inline format/preset combos, progress bars)
    ExportSettingsDialog.*  The big settings dialog with live preview
    PresetBrowser.*, MediaBrowser.*, WatchFolderPanel.*, EncodingPanel.*, PreferencesDialog.*
    FrameGrabber.*          Async single-frame previews via FFmpeg
    Theme.*                 Dark theme and runtime-drawn icons
  main.cpp                  App start-up and the command-line interface
tests/tst_core.cpp          Integration tests (real FFmpeg encodes)
scripts/                    fetch-ffmpeg.ps1, package.ps1
installer/                  Inno Setup script
```

Key design points:

- **FFmpeg runs as a separate process.** We never link FFmpeg libraries; `CommandBuilder` is
  the single place that knows FFmpeg's command-line syntax. That keeps licensing simple and
  lets users swap in any FFmpeg build.
- **`core` has no widget dependency.** New features should put their logic in `core` with tests,
  and keep `ui` thin.
- **Everything an output needs is in `ExportSettings`.** Presets, queue items and watch-folder
  outputs all store the same JSON, so adding a field means updating `toJson`/`fromJson` (and
  the round-trip test covers it).

## Adding a preset

System presets live in `PresetManager::buildSystemPresets()` (`src/core/PresetManager.cpp`).
Most presets are a few lines:

```cpp
s = base("h264", "Instagram Reels 1080x1920");
frame(s, 1080, 1920, 30, "fill");
bitrate(s, 12, 16);
add(s);
```

The test suite automatically encodes **every** system preset and checks that the output is
readable and has the requested frame size, so `.\build.ps1 -Test` verifies your preset end to end.

## Adding a format or codec

1. Add the codec to `buildVideoCodecs()` / `buildAudioCodecs()` in `src/core/Formats.cpp`
   (its speed presets, profiles, pixel formats and supported rate-control modes drive the UI).
2. Add or extend a format in `buildFormats()`.
3. If the encoder needs special arguments, handle it in `appendVideoCodecArgs()` in
   `src/core/CommandBuilder.cpp`.
4. Add at least one system preset that uses it so it's covered by the tests.

## Coding conventions

- C++17 and Qt 6 idioms (signals/slots with function pointers, `QStringLiteral`, no raw `new`
  without a parent).
- Formatting is defined in `.clang-format`. Run it on files you change:
  `clang-format -i src/core/MyFile.cpp` (Qt Creator ships `clang-format`).
- Keep comments for the *why*; the code should explain the *what*.
- User-visible text should be clear and concise; error messages should say what to do next.
- No new dependencies without discussing them in an issue first.

## Tests

`tests/tst_core.cpp` contains integration tests that generate media with FFmpeg and run real
encodes: every system preset, the full effects chain, two-pass, stitching, stills, pause/resume,
watch folders and queue persistence. They take a few minutes. Run a subset with Qt Test's
filtering:

```powershell
.\build\core_tests.exe effectsChain twoPass
```

Please add or extend a test for any behaviour change in `core`.

## Pull requests

1. Fork the repo and create a branch from `main` (`feature/...` or `fix/...`).
2. Keep each PR focused on one change; split unrelated changes up.
3. Make sure `.\build.ps1 -Test` passes and the code is clang-formatted.
4. Update `CHANGELOG.md` under **Unreleased** for user-facing changes.
5. Fill in the PR template. Screenshots or a short clip help a lot for UI changes.

CI builds with MinGW and MSVC, runs the tests and produces downloadable packages for every PR,
so reviewers can try your change without building it.

## Releasing (maintainers)

1. Update the version in `CMakeLists.txt` and move **Unreleased** notes in `CHANGELOG.md` under the new version.
2. Commit, then tag: `git tag v1.2.3 && git push origin v1.2.3`.
3. The **Build** workflow builds, tests and packages the release, then publishes a GitHub
   Release with the installer, the portable zip and checksums.

## License

By contributing, you agree that your contributions are licensed under the [MIT License](LICENSE).
