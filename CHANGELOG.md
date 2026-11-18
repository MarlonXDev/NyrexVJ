# Changelog

## 0.3.0 - 2026-11-18

- Audio reactivity: pick system audio (for example your DJ program), a microphone or line-in, or a demo signal in the new Audio tab. Live spectrum, bass, mid, high and level meters, beat and tempo detection and an optional BPM sync.
- Layers, the master and every effect can follow Bass, Mid, High, Level or Kick: opacity, scale, speed, hue, brightness, saturation and any effect parameter.
- Spout output: send the picture straight to OBS under a sender name of your choice. In OBS add the source "Spout2 Capture" and pick the sender. Windows only.
- Welcome screen on the first start and after every update, listing everything that is new. Reopen it any time with Help, What's new.
- English and German interface. English is the default, the language can be switched at any time under Help, Language, in the Settings tab or on the welcome screen.
- 16 new clips: Mandelbrot, Metaballs, Voronoi, Lightning, Fire, Rings Burst, Hex Grid, Warp Speed, Oscilloscope, Spectrum Circle, Liquid Marble, Cube Tunnel 3D, Gyroid 3D, Terrain 3D, Sunburst, Digital Rain. Bars and Equalizer now show the real spectrum.
- 33 new effects, 56 in total: Radial Blur, Light Rays, Chromatic Aberration, VHS, CRT, Halftone, Hex Mosaic, Slice Shift, Shockwave, Ripple, Polar Coordinates, Infinite Zoom, Flow Displace, Duotone, Solarize, Neon Edge, Sharpen, Camera Shake, Film Grain, Threshold, Color Echo, Spectrum Warp, Oscilloscope, Pixel Smear, Anamorphic Streak, Dither, Block Glitch, Prism and the 3D effects Sphere Map, Cube Spin, Cylinder Wrap, Floor Reflect and Plane 3D.
- 7 new factory presets, 15 in total, several of them audio reactive: Audio Reactor, Fractal Dream, Cyber Rain, Inferno, 3D Worlds, Crystal Cube, Pop Art.
- Shaders compile when they are first used and clip thumbnails appear one after another, so the program starts much faster.
- Cleaner repository: the source is split into app, audio, core, render, update and platform, the program has an icon, and there are new tests for audio analysis, languages and the changelog.

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
