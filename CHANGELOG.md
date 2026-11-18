# Changelog

## 0.9.0 - 2026-11-18

- Clip Creator: the new Creator menu opens a window where you build your own 3D scenes. Add spheres, cubes, cylinders, cones, planes, tori, torus knots and your own .glb models, place, rotate and scale them, repeat any object in a ring or a line, add up to four lights, and see the result live in a preview you can orbit with the mouse. Undo, redo and four ready made templates (the new Mesh scenes) are included.
- Your scenes are saved as .nyxclip files in the new clips folder. A .nyxclip is a single portable file that carries the scene together with the models and textures it uses, so you can send it to a friend and it plays without anything else. Use as clip saves the scene and puts it into the selected cell at once.
- Everything can move with the music: every object, light and the camera can react to Bass, Mid, High, Level or Kick (bigger, glowing, twisting, lifted, brighter, zooming), and every object can spin, bob, orbit, pulse or sway on its own.
- Realistic lighting for 3D clips and imported models: metallic and rough surfaces, normal maps, glowing materials, cut-out transparency, reflections of a built-in sky and up to four lights. Models with PBR materials now look the way their authors made them. The old built-in 3D clips keep their look.
- 4 new 3D clips made with the Creator, 50 clips in total: Chrome Orbs Mesh, Neon Cubes Mesh, Gold Knot Mesh and Pulse Columns Mesh. They follow the layer colours where it makes sense and react to bass, mid, high and kick.
- Library: the new Scenes filter shows only your own scenes, the new Assets mode lists your models, textures and scenes with their sizes and opens their folders, and a right click on a scene clip offers Edit in the Clip Creator.
- Less memory: a 3D scene that is in no cell and was not used for two minutes is unloaded and loads again when you need it. Its thumbnail stays.

## 0.8.0 - 2026-11-18

- New window layout in the style of Resolume Arena: the clip grid with its layer headers fills the top, the deck buttons and the tempo row sit right below it, the preview and the output are at the bottom left, Master and Layer share one tabbed window, the selected clip has its own Clip window and the Browser with Presets, Library and Audio is on the right. The window layout is reset once by this update (Settings, Appearance, Reset window layout does it any time).
- Pause button in the tempo row: it freezes all motion, clips, beat and layer clocks, while deck transitions keep running. The new Layer tab next to Master and the new Clip window collect everything about the selected layer and clip, including the controls of 3D models.
- 8 new clips, 46 in total: Neon Sigil (glowing sacred geometry), Neon Lattice, Neon Corridor, Chaos Smoke, Tesseract (a rotating 4D cube), Laser Web, Mandelbox Fly (a flight through a lit fractal) and Prism Bloom. All of them follow the layer colours and move with the music.
- 8 new effects, 76 in total: Kaleido Neon, Mandala Fold, Hue Feedback, Star Burst, Chrome Liquid, Image Terrain, Hyperspace and Shatter. They turn any picture into neon kaleidoscopes, endless mandalas, colour-shifting feedback trails, light stars, liquid chrome, a landscape you fly over, a jump into hyperspace or shattering glass that bursts on the kick.
- 8 new, harder factory presets, 26 in total: Hyperdrive, Neon Cathedral, Chrome Meltdown, Shatter Storm, Mandala Machine, Terrain Flight, Tesseract Club and Feedback Galaxy. "Fill the decks with factory looks" now starts with these eight.
- New things are marked with a small "new" behind their name in the Presets list, the Library clips and the Library effects. The marker belongs to the version that added the item and disappears by itself with the next minor version, so it only ever shows what is really new.
- 3D models: the Library has a Scan again button that finds new model files without restarting, and loading a model now has a time limit of 60 seconds and a Cancel loading button, so a huge file can no longer hold up the load for long.

## 0.7.0 - 2026-11-18

- Library: click a clip to put it into the selected cell and play it, or drag it onto any cell or onto a column header, which fills the selected layer. The Library has a search box, filters (All, 2D, 3D, Models), larger tiles and a right click menu. Dragging from the Library did not start before, this is fixed.
- Effects in the Library: click an effect to add it to the first free slot of the selected layer, drag it onto an effect slot, or right click it to choose a layer or the master. The effect list in the layer and master panels now has a search box.
- Themes: Settings, Appearance offers nine colour themes (Orange, Blue, Red, Green, Purple, Pink, Cyan, Gold and Mono). Orange is the default and a little brighter than before. The choice applies at once and is remembered.
- Clearer interface: the clip grid marks the selected cell and the columns that are playing and explains itself with tooltips, and every settings page has a short description under its title. The window layout gives the Library more room and is reset once by this update (Settings, Appearance, Reset window layout does it any time).
- 10 new 3D effects, 68 in total: Crystal Tunnel, Mirror Corridor, Frame Tunnel, Crystal Facets, Relief Light, Glass Orb, Wormhole, Carousel, Tilt Shift and Parallax Stack. They turn any clip into mirrored crystal tunnels, corridors, glass, lit relief and floating cards.

## 0.6.0 - 2026-11-18

- 3D clips: three new clips that are real 3D models instead of shaders, Torus Knot Mesh, Crystal Cluster Mesh and Orbit Rings Mesh. They have depth, lighting and animation, follow the layer colours, speed and scale, pulse with bass and kick and take effects like every other clip.
- Your own 3D models: put .glb or .gltf files, with textures and animation, into the new models folder (the Library tab opens it, restart NyrexVJ once) and they appear in the Library as Model: name. Models load in the background without stalling the picture, and damaged or oversized files are refused with a message instead of crashing.
- Library tooltips: point at a 3D clip to see its vertices, triangles, graphics memory and load time. Layers that play the same model share it in memory.
- Measuring mode for developers: NyrexVJ --bench-scene 4:300:"Torus Knot Mesh" reports frame times and memory with several 3D layers at once.

## 0.5.0 - 2026-11-18

- Live control over OSC: switch it on under Settings, Control (off by default, port 9010, only this computer unless you allow others). NyrexVJ then answers to 207 addresses under /nyrex/ for the master, the 16 decks, the 8 columns, every clip, layer opacity, speed, scale and colour, bypass, solo, clear and all effect switches and parameters. Send a value from 0 to 1, or nothing at all to fire a trigger. It works with TouchDesigner, Max, Pure Data, Python scripts and anything else that speaks OSC.
- MIDI controller mapping on Windows: pick your controller, press Add mapping, choose what to control and move a knob or press a key to link them. Faders and knobs set values, keys trigger clips, columns and decks or switch things on and off, and Invert flips a fader. Mappings are saved in controls.json.
- New Control page in the Settings window with the status of OSC and MIDI, the last message that arrived, the mapping list with Learn and Remove, and a searchable list of every OSC address that copies it with one click.
- Updates are sturdier on Windows: if Windows refuses to swap the running program (error 5), NyrexVJ now retries, picks a free backup name and, if that still fails, finishes the update right after you close it. A failed update shows a button that opens the download page.

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
