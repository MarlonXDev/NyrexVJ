# Changelog

## 0.5.0 - 2026-11-18

- Live control over OSC: switch it on under Settings, Control (off by default, port 9010, only this computer unless you allow others). NyrexVJ then answers to 207 addresses under /nyrex/ for the master, the 16 decks, the 8 columns, every clip, layer opacity, speed, scale and colour, bypass, solo, clear and all effect switches and parameters. Send a value from 0 to 1, or nothing at all to fire a trigger. It works with TouchDesigner, Max, Pure Data, Python scripts and anything else that speaks OSC.
- MIDI controller mapping on Windows: pick your controller, press Add mapping, choose what to control and move a knob or press a key to link them. Faders and knobs set values, keys trigger clips, columns and decks or switch things on and off, and Invert flips a fader. Mappings are saved in controls.json.
- New Control page in the Settings window with the status of OSC and MIDI, the last message that arrived, the mapping list with Learn and Remove, and a searchable list of every OSC address that copies it with one click.

## 0.4.0 - 2026-11-18

- Decks: set up to 16 decks, each with its own layers, clips, colours and effects, and switch between them with a smooth transition instead of a hard cut. Seven transitions: Fade, Dissolve, Zoom, Wipe, Radial, Spin and Slide, with a length in seconds or in beats and an optional auto cycle. Click the deck buttons above the clip grid, press Page Up and Page Down, or use Ctrl and 1 to 8 (with Shift for decks 9 to 16). Right click a deck to rename, copy, fill or clear it. All decks are saved in presets.
- 3 new clips inspired by Resolume looks: Milky Wave (flowing contour lines), Crystal Cavern (dark mirrored crystal tunnel) and Crystal Lines (the same tunnel as white line art). All of them move with the music.
- 2 new effects, 58 in total: Line Art turns any picture into glowing outlines and Mirror Fold mirrors it into crystal shapes.
- Effects can now react only to audio: switch an effect from Parameter to Gate and it shows up only while Bass, Mid, High, Level or Kick is loud, and fades out in silence.
- 3 new factory presets, 18 in total: Milky Red, Crystal Cavern and Crystal Lines. A factory look now loads into the current deck, and one button fills all 16 decks with factory looks.
- New Settings window under Settings in the top menu (or Ctrl and comma) with General, Audio, Output, Updates, Interface, Shortcuts and About. Output window, Spout, resolution, VSync, audio analysis, language and interface size all live there now.
- Updates: NyrexVJ checks for a new version on every start and prepares it in the background. A small card appears at the top right with Restart now or Later, and stays visible as a slim badge until you restart. Automatic download can be turned off.
- Clearer layout: a status bar with FPS, tempo, deck, audio and Spout state, collapsible sections in the layer and master panels, tooltips and an adjustable interface size. The audio tab warns when the chosen device delivers no sound.

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
