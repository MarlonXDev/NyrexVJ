<p align="center">
  <img src="docs/icon.png" alt="NyrexVJ" width="96">
</p>

<h1 align="center">NyrexVJ</h1>

<p align="center">
  Live visuals for VRChat events and parties. Native C++ and OpenGL, no game engine, one Windows executable.<br>
  <a href="README.de.md">Deutsch</a> · <a href="CHANGELOG.md">Changelog</a> · <a href="https://github.com/MarlonXDev/NyrexVJ/releases">Releases</a>
</p>

<p align="center">
  <img src="docs/screenshot-main.png" alt="NyrexVJ main window" width="100%">
</p>

NyrexVJ plays generative clips on layers, stacks effects on top, follows your music and sends the picture to OBS with Spout or to a fullscreen output window. It is built in the spirit of Resolume Arena, small enough to start in a second.

> Status: v0.3.0. Clips, effects, presets, audio reactivity, Spout output, the output window, English and German and the auto-updater work. Video clips and MIDI are not implemented yet.

## Download

Get `NyrexVJ.exe` from the [Releases page](https://github.com/MarlonXDev/NyrexVJ/releases). It is a single file, no installer needed. Windows SmartScreen may warn because the executable is not code-signed: choose "More info" and "Run anyway".

The release also contains `NyrexVJ-windows-x64.zip` (exe, changelogs, readmes) and `SHA256SUMS.txt`.

## Highlights

- **Audio reactive.** Pick system audio (your DJ program), a microphone, a line-in or a demo signal. Layers, the master and every effect parameter can follow Bass, Mid, High, Level or Kick.
- **OBS in two clicks.** Spout output with a sender name of your choice. In OBS add the source "Spout2 Capture" and select the sender.
- **32 clips, 56 effects, 15 presets.** Fractals, raymarched 3D scenes, lightning, fire, spectrum visuals and more.
- **English and German.** English is the default, switch any time under Help, Language.
- **Welcome screen.** Shown on the first start and after every update, with everything that is new.
- **Self-updating.** Checks GitHub, verifies the SHA-256 sum and restarts into the new build.

<p align="center">
  <img src="docs/screenshot-audio.png" alt="Audio reactivity" width="100%">
</p>

## Audio

Open the Audio tab in the Browser panel.

| Source | What it listens to |
| --- | --- |
| Output devices (loopback) | Whatever your PC plays, for example Rekordbox, Serato, Spotify or a browser |
| Input devices | Microphone or line-in |
| Demo signal | A built-in beat so you can build looks without music |

The tab shows the live spectrum, Bass, Mid, High and Level meters, beat detection and a tempo estimate. Gain, smoothing and beat threshold are adjustable, and "Sync BPM to audio" locks the deck tempo to the detected beat.

Reactivity is set in three places:

- **Layer panel**: two modulation slots per layer (band to opacity, scale, speed, hue, brightness or saturation, with an amount).
- **Master panel**: the same for the final picture.
- **Every effect slot**: a band, a parameter and an amount.

Bars, Equalizer, Spectrum Circle, Oscilloscope, Spectrum Warp and the Oscilloscope effect draw the real spectrum or waveform. Without an audio source they run on a virtual beat that follows the BPM.

## Spout and OBS

1. In NyrexVJ tick "Send to OBS via Spout" in the Master panel (or the Output menu) and choose a sender name.
2. In OBS install the [obs-spout2-plugin](https://github.com/Off-World-Live/obs-spout2-plugin) once.
3. Add a source "Spout2 Capture" and pick the sender from the list.

Spout is Windows only. The output window (F11) with OBS window capture keeps working as an alternative.

## Clips and effects

<p align="center">
  <img src="docs/gallery-clips.jpg" alt="All 32 clips" width="100%">
</p>

Every layer has four effect slots, and so does the master.

<p align="center">
  <img src="docs/gallery-effects.jpg" alt="All 56 effects" width="100%">
</p>

The effect gallery shows each effect with default settings on the Plasma clip (Duotone on Mandelbrot, because Plasma has almost no brightness contrast).

## Presets

15 factory looks: Neon Club, Acid Rainbow, Deep Space, Synthwave Night, Laser Show, Minimal Mono, 3D Showcase, Warp Tunnel, Audio Reactor, Fractal Dream, Cyber Rain, Inferno, 3D Worlds, Crystal Cube and Pop Art. Save your own in the Presets tab. The last session is restored on start.

## Controls

| Input | Action |
| --- | --- |
| Click a cell | Play that clip on its layer |
| Click a column header, or keys 1 to 8 | Trigger the whole column |
| Space | Tap tempo |
| B | Blackout |
| F11 | Fullscreen output window |
| Esc | Leave fullscreen |
| Right-click a cell | Assign or clear a clip |
| Right-click a slider | Reset it to its default |
| Drag from Library to a cell | Assign a clip |
| B / S / X on a layer | Bypass / solo / stop the layer |

## Languages

English is the default. German is built in. Change it under Help, Language, in the Settings tab or on the welcome screen, the choice is saved. Clip and effect names stay in English so they match what other VJ programs call them. A new language needs a translation table next to `src/core/i18n_de.cpp` and an entry in `src/core/i18n.cpp`.

## Auto-update

NyrexVJ checks GitHub on start (switch it off in the Settings tab). When a newer build exists the menu bar shows a notice with an Install button.

| Channel | Source | Updates when |
| --- | --- | --- |
| Latest build (default) | rolling `nightly` pre-release | every commit on `main` |
| Stable | latest GitHub Release | a new version tag is published |

The updater only works while the repository is public.

## Project layout

```
NyrexVJ/
├── src/
│   ├── app/        Dear ImGui interface: deck, panels, browser, welcome screen, theme
│   ├── audio/      WASAPI capture and loopback, FFT analysis, beat and tempo detection
│   ├── core/       Composition model, presets, settings, i18n, log, SHA-256
│   ├── render/     OpenGL engine, clip and effect libraries, output window, Spout
│   ├── update/     Update check, installer, changelog parser
│   ├── platform/   Windows and fallback platform code
│   └── main.cpp
├── tests/          Unit tests for core, audio, i18n and update logic
├── scripts/        Repository checks (translation coverage)
├── resources/      Windows icon and resource template
├── docs/           Images used by this readme
├── third_party/    Vendored glad and nlohmann/json
└── .github/        CI, issue templates
```

## Build from source

Requirements: CMake 3.21 or newer, a C++20 compiler and a GPU with OpenGL 3.3.

Windows with Visual Studio:

```
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

Linux needs the X11 development packages that GLFW requires (Xrandr, Xinerama, Xcursor, Xi):

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

GLFW, Dear ImGui and Spout2 (Windows) are downloaded by CMake during configuration. Audio capture and Spout are Windows only, on other systems the audio demo signal is used.

## Tests

```
ctest --test-dir build
python scripts/check_i18n.py
NyrexVJ --check-shaders
```

`check_i18n.py` makes sure every UI string has a German translation. `--check-shaders` compiles every clip and effect and exits with code 1 on any failure. CI runs all of it on every push.

## Releasing

1. Bump the version in `CMakeLists.txt` (`project(NyrexVJ VERSION x.y.z)`) and add a section to `CHANGELOG.md` and `CHANGELOG.de.md`.
2. Commit, then tag and push: `git tag vx.y.z && git push origin vx.y.z`.
3. GitHub Actions builds, tests and publishes the Release with `NyrexVJ.exe`, the zip and `SHA256SUMS.txt`. The release text comes from the matching `CHANGELOG.md` section.

Every push to `main` also refreshes the `nightly` pre-release that the default update channel follows.

## Command line

| Option | Effect |
| --- | --- |
| `--preset NAME` | Load a factory or user preset on start |
| `--column N` | Trigger column N (1-based) on start |
| `--clip NAME` | Start with an empty deck that plays only this clip |
| `--effect NAME` | Put an effect in the first master effect slot |
| `--select-layer N` | Select layer N (1-based) |
| `--resolution IDX` | Output resolution index |
| `--tab IDX` | Browser tab to show |
| `--output` | Open the output window on start |
| `--lang en\|de` | Start in this language |
| `--audio-demo` | Use the demo audio signal |
| `--welcome` / `--no-welcome` | Force or skip the welcome screen |
| `--fresh` | Ignore the saved session |
| `--no-update-check` | Skip the update check |
| `--check-shaders` | Compile every shader and exit |
| `--frames N` | Run N frames at a fixed 60 FPS step, then exit |
| `--screenshot FILE.png` | With `--frames`, save the window as PNG |
| `--dump-output FILE.png` | With `--frames`, save the engine output as PNG |

User data lives in `%APPDATA%\NyrexVJ` on Windows and `~/.config/NyrexVJ` on Linux: log, window layout, settings, autosave and `presets/`. Set `NYREXVJ_DATA_DIR` to use another folder.

## Roadmap

- Video clips (H.264 and HAP) through FFmpeg
- MIDI controller mapping
- OSC input
- DMX strip output for VRSL
- Custom shader clips loaded from files
- More languages

## Third-party software

- [GLFW](https://www.glfw.org/) (zlib license)
- [Dear ImGui](https://github.com/ocornut/imgui) (MIT)
- [Spout2](https://github.com/leadedge/Spout2) (BSD 2-Clause), Windows only
- [glad](https://github.com/Dav1dde/glad) generated OpenGL loader (WTFPL or CC0, plus Apache-2.0 for the Khronos parts)
- [stb_image_write](https://github.com/nothings/stb) (public domain or MIT)
- [nlohmann/json](https://github.com/nlohmann/json) (MIT)

## License

MIT, see [LICENSE](LICENSE).
