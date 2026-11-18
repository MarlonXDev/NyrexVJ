# NyrexVJ

Open-source VJ software for VRChat events and live visuals. Native C++ and OpenGL, no game engine, ships as a single Windows executable.

NyrexVJ is built for the typical VRChat event chain: it renders a 1920x1080 composition and (planned) hands it to OBS through Spout, so it can be streamed into a world together with DMX lighting.

> Status: early development (v0.1.0). The core engine and the user interface work; video clips, Spout output and MIDI are not implemented yet.

## Features

- Clip deck with layers and columns, trigger single clips or a whole column
- 8 built-in generative clips written in GLSL, all synced to the BPM
- Per-layer opacity, blend mode (Normal, Add, Screen, Multiply), bypass and solo
- BPM with tap tempo and beat resync
- Master output with hue shift, pixelate, RGB split, mirror X/Y and invert
- Output resolution 1280x720, 1920x1080 or 2560x1440
- Dockable interface, the window layout is saved between sessions
- Drag clips from the library onto deck cells, right-click a cell to assign or clear it

## Controls

| Input | Action |
| --- | --- |
| Click a cell | Play that clip on its layer |
| Click a column header, or keys 1 to 8 | Trigger the whole column |
| Space | Tap tempo |
| Right-click a cell | Assign or clear a clip |
| Drag from Library to a cell | Assign a clip |
| B / S / X on a layer | Bypass / solo / stop the layer |

## Roadmap

- Video clips (H.264 and HAP) through FFmpeg
- Spout output for OBS
- MIDI controller mapping
- OSC input
- DMX strip output for VRSL
- Save and load compositions
- Custom shader clips loaded from files

## Download

Every push builds a Windows executable with GitHub Actions. Download it from the Actions tab (artifact `NyrexVJ-windows-x64`) or, once a version tag exists, from the Releases page.

## Build from source

Requirements: CMake 3.21 or newer, a C++20 compiler and a GPU with OpenGL 3.3.

Windows with Visual Studio:

```
cmake -S . -B build -A x64
cmake --build build --config Release
```

Linux needs the X11 development packages that GLFW requires (Xrandr, Xinerama, Xcursor, Xi):

```
cmake -S . -B build -G Ninja
cmake --build build
```

Dependencies (GLFW, Dear ImGui) are downloaded by CMake during configuration.

## Command line

| Option | Effect |
| --- | --- |
| `--column N` | Trigger column N on start |
| `--frames N` | Run N frames at a fixed 60 FPS step, then exit |
| `--screenshot FILE.png` | With `--frames`, save the final frame as PNG |

The log file is written to `%APPDATA%\NyrexVJ\nyrexvj.log` on Windows and `~/.config/NyrexVJ/nyrexvj.log` on Linux.

## Third-party software

- [GLFW](https://www.glfw.org/) (zlib license)
- [Dear ImGui](https://github.com/ocornut/imgui) (MIT)
- [glad](https://github.com/Dav1dde/glad) generated OpenGL loader (WTFPL or CC0, plus Apache-2.0 for the Khronos parts)
- [stb_image_write](https://github.com/nothings/stb) (public domain or MIT)

## License

MIT, see [LICENSE](LICENSE).
