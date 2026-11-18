# Changelog

## 0.2.0 - 2026-11-18

- Auto-updater: NyrexVJ checks GitHub for new builds and installs them after a checksum check. Two channels: latest build (every commit) and stable releases.
- Presets: save, load and delete your own looks. Eight factory presets. The last session is restored on start.
- 8 new clips, five of them 3D or pseudo-3D: Cube Field 3D, Torus 3D, Sphere Grid 3D, Synthwave, Starfield, Laser Fan, Equalizer, Julia.
- 23 effects with four slots on every layer and on the master, including the 3D effects Perspective Plane, Tunnel Map and Fisheye.
- Colour settings per layer: two colours, rainbow mode, palettes, speed and scale, plus hue, saturation, brightness and contrast on every layer and on the master.
- Clip thumbnails and live layer previews in the deck.
- Separate output window with fullscreen and display selection (F11), usable with OBS window capture.
- Resolutions from 960x540 to 2560x1440, tap tempo, BPM halving and doubling, blackout.
- Unit tests for presets, versions, checksums and the update flow.
- Release workflow: tagged builds publish NyrexVJ.exe, a zip and checksums as a GitHub Release, every push to main publishes a nightly build.

## 0.1.0

- First skeleton: four layers, eight columns, eight clips, master effects, docking layout.
