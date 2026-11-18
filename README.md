# NyrexVJ

Open-source VJ software for VRChat events and live visuals. Native C++ and OpenGL, no game engine, ships as a single Windows executable.

NyrexVJ renders a composition of layers, clips and effects in real time, shows it in a separate output window and updates itself from GitHub.

> Status: v0.2.0. Generative clips, 3D effects, presets, output window and auto-updater work. Video clips, Spout output and MIDI are not implemented yet.

## Download

Get `NyrexVJ.exe` from the [Releases page](https://github.com/MarlonXDev/NyrexVJ/releases). It is a single file, no installer needed. Windows SmartScreen may warn because the executable is not code-signed: choose "More info" and "Run anyway".

The release also contains `NyrexVJ-windows-x64.zip` and `SHA256SUMS.txt`.

## Auto-update

NyrexVJ checks GitHub on start (switch it off in the Settings tab). When a newer build exists, the menu bar shows a notice with an Install button. The update is downloaded, checked against the SHA-256 sum from the release, swapped in place and the program restarts.

| Channel | Source | Updates when |
| --- | --- | --- |
| Latest build (default) | rolling `nightly` pre-release | every commit on `main` |
| Stable | latest GitHub Release | a new version tag is published |

The updater only works while the repository is public.

## Features

- 4 layers x 8 columns clip deck with thumbnails and live layer previews
- 16 generative GLSL clips, 3 of them raymarched 3D (Cube Field, Torus, Sphere Grid), plus Synthwave, Starfield, Laser Fan, Equalizer, Julia and more
- 23 effects in 4 slots per layer and on the master, including 3D effects (Perspective Plane, Tunnel Map, Fisheye), Trails, Glitch, Kaleidoscope, Glow and Hue Cycle
- Colour settings per clip layer: two colours, palettes, rainbow mode, speed, scale
- Colour grading (hue, saturation, brightness, contrast) per layer and on the master
- Per-layer opacity, blend mode (Normal, Add, Screen, Multiply), bypass and solo
- BPM with tap tempo, resync, halve and double
- 8 factory presets, save and load your own, the last session is restored on start
- Output window with fullscreen on a chosen monitor, usable with OBS window capture
- Output resolutions from 960x540 to 2560x1440
- Dockable interface, the layout is saved between sessions

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

## Roadmap

- Video clips (H.264 and HAP) through FFmpeg
- Spout output for OBS
- MIDI controller mapping
- OSC input
- DMX strip output for VRSL
- Custom shader clips loaded from files

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

Dependencies (GLFW, Dear ImGui) are downloaded by CMake during configuration.

## Releasing

1. Bump the version in `CMakeLists.txt` (`project(NyrexVJ VERSION x.y.z)`) and add a section to `CHANGELOG.md`.
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
| `--fresh` | Ignore the saved session |
| `--no-update-check` | Skip the update check |
| `--frames N` | Run N frames at a fixed 60 FPS step, then exit |
| `--screenshot FILE.png` | With `--frames`, save the window as PNG |
| `--dump-output FILE.png` | With `--frames`, save the engine output as PNG |

User data lives in `%APPDATA%\NyrexVJ` on Windows and `~/.config/NyrexVJ` on Linux: log, window layout, settings, autosave and `presets/`. Set `NYREXVJ_DATA_DIR` to use another folder.

## Third-party software

- [GLFW](https://www.glfw.org/) (zlib license)
- [Dear ImGui](https://github.com/ocornut/imgui) (MIT)
- [glad](https://github.com/Dav1dde/glad) generated OpenGL loader (WTFPL or CC0, plus Apache-2.0 for the Khronos parts)
- [stb_image_write](https://github.com/nothings/stb) (public domain or MIT)
- [nlohmann/json](https://github.com/nlohmann/json) (MIT)

## License

MIT, see [LICENSE](LICENSE).
