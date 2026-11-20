# Changelog

## 0.19.0 - 2026-11-20

- Hidden layers cost almost nothing: a layer whose opacity is at 0 (or 0.1 percent and below, including audio modulation) is no longer drawn, neither its clip nor its effects nor its blend pass. Its preview in the layer strip is still refreshed, at about 7.5 times a second instead of every frame, and a layer with a feedback effect such as Trails is always drawn so its trails do not reset. In the test environment a composition with three of four layers faded out needed 47 instead of 146 ms per frame, one with opacities 1, 0.5, 0.25 and 0 needed 96 instead of 134 ms, and a composition with all layers visible is unchanged. The output is identical to the picture before, pixel by pixel.
- Layer resolution: Settings, Performance, Render quality draws the clips and effects of every layer at 75 percent (Balanced), at 50 percent (Performance) or lets NyrexVJ choose (Automatic, 100 down to 50 percent in small steps, only while a frame takes longer than your frame budget). The master effects, the colour and level stage, the transitions and every output stay at full resolution. In the test environment frame times fell by 38 to 40 percent at 75 and by 66 to 67 percent at 50. Full quality is the default and changes nothing. Effects that work on the pixel grid (Pixelate, Halftone, Scanlines and the like) look coarser at the lower settings.
- Less video memory: a render target that was allocated for transitions and never used is gone, the two transition pictures are freed five seconds after the last transition (15.8 MiB at 1080p) and the reduced layer resolution frees the full-size layer pictures (27.7 instead of 39.6 MiB of render targets at 1080p and 50 percent).
- Pass profiler: Settings, Performance, Where the GPU time goes shows, after you tick Measure every render pass, the GPU time of every clip, every effect, every blend and the final stage with its share, and warns about a pass that takes a quarter or more. It is off at start and not saved. The measurements are new command line options (--profile with --bench-comp, --bench-shaders, --bench-memory, --quality) and a script that writes the benchmark presets, see docs/PERFORMANCE.md.
- What was measured and what was not: everything above comes from a software OpenGL driver without a graphics card, so the saving of skipped work and fewer pixels carries over but the milliseconds do not. Nothing was measured next to VRChat or in VR. Several ideas were tried or weighed and left out (constant tables in the blur and glow shaders gave no gain), see docs/adr/0011-render-pipeline-optimisation.md. Please run NyrexVJ --bench-shaders 30 on your computer and share the file.

## 0.18.0 - 2026-11-20

- Hardware video decoding: H.264, HEVC, AV1 and VP9 can be decoded by the graphics card (Direct3D 11, DXVA2, NVIDIA CUDA and Intel QSV on Windows, NVIDIA CUDA, VA-API, Intel QSV and VDPAU on Linux). Settings, Performance, Decoder offers Automatic (large videos on the graphics card, small ones on the processor), Prefer hardware and Force software. NyrexVJ tests every interface once for H.264, HEVC and AV1 and shows the result. If the graphics card cannot decode a video, or stops while it plays, the processor takes over by itself and the reason is shown. This was written against FFmpeg's interfaces and could not be tried on a real graphics card yet, so please report what happens on yours.
- HAP, HAP Alpha and HAP Q go to the graphics card as compressed textures without being unpacked on the processor. In the test environment the processor time for HAP fell from 26 to 6 percent of one core (1080p) and from 105 to 10 percent (4K), and four HAP videos in 1080p use 8 instead of 32 MB of video memory. Without S3TC support, or for an unusual HAP file, the processor decoder is used and the monitor says why.
- Other videos: the colour conversion and the scaling moved from the processor to the graphics card (8 to 16 bit, with alpha, with the right colour matrix). Four H.264 videos in 1080p need 52 instead of 76 percent of one core. The price is more video memory (14 instead of 8 MB per 1080p video); the setting Colours converted on the graphics card turns it off.
- Frame rate limit: Settings, Performance, Frame rate limit with 30, 60, 90, 120, 144 or Unlimited, independent of VSync and with an exact pause between frames, so the processor rests and the frame times stay even. It is now 60 by default, so NyrexVJ no longer draws 144 frames a second on a fast monitor. Choose Unlimited for the old behaviour.
- Video monitor: Settings, Performance, Videos shows for every playing video the decoder and the hardware interface, the route to the graphics card, the reason for a fallback, decode time, upload time, dropped and late pictures and the video memory, split into pictures, colour planes, HAP textures and upload buffers. The Clip window shows the decoder of its clip. The memory numbers are estimates.
- Fixed: a looping video lost a few pictures at every restart. New command line options measure the cost of videos (--bench-video) and check the graphics card path against the processor path (--video-selftest), see docs/MEDIA.md and docs/adr/0010-video-decode-pipeline.md. Direct hand-over of decoded pictures to OpenGL (zero copy) is not implemented and nothing was measured in VR or next to VRChat.

## 0.17.0 - 2026-11-20

- Unreal Engine plugin (experimental): the folder integrations/unreal/NyrexVJ is a plugin with Blueprint nodes for the music data of NyrexVJ (audio levels, spectrum, BPM, beat number and phase, your named parameters) and the events On Beat, On Onset, On Connected and On Disconnected, and with nodes that send controls back (master level, layer opacity, column, deck, tap or any /nyrex/ address). Copy the folder into the Plugins folder of your project. It has never been compiled with Unreal Engine, only its protocol part is tested here, so expect to fix small things, see docs/UNREAL.md.
- The protocol part of the plugin is tested on every build against the real OSC sender and parser of NyrexVJ, including damaged and cut-off packets, size limits and an unknown schema number, so a change to the OSC output cannot silently break it any more.
- New page docs/INTERFACES.md lists every network protocol, file format and folder that other programs or older versions can touch, with version numbers and the rule for changing each one.

## 0.16.0 - 2026-11-19

- Shader clips from files: put .glsl, .frag or .fs files into the new shaders folder (or drag them onto the window) and they appear in the Library as clips (filter Shaders). Both styles work and are found automatically: NyrexVJ style with void main() and Shadertoy style with mainImage. A saved change reloads the shader and its thumbnail while it plays, a compile error shows with the line number of your file in the Clip window, and Create example shader writes a working starting point. See docs/SHADERS.md.
- Spanish and French: both are built in (Settings, General, Help, Language and the welcome screen). They were translated with machine help and are not reviewed by native speakers yet, so corrections are welcome on GitHub. Clip and effect names stay in English, and the changelog on the welcome screen is shown in English except for German.
- Your own languages: put a .json file into the new languages folder, press Reload languages, and the language shows up in every language list. Export translation template writes a file with every text to start from, and texts you leave out stay English. The built-in font only shows Latin letters, so other scripts are not possible yet. See docs/LANGUAGES.md.

## 0.15.0 - 2026-11-19

- Stage control: the new Settings page Stage turns controls into DMX. Every channel (universe 1 to 4, channel 1 to 512, optional 16 bit) follows a fixed value or any modulation source: an audio band, an LFO, a macro, the beat or the bar, so the same sources that move an effect can move a light. Test patterns (all on, ramp, chase) check the setup.
- Art-Net output sends the channels as ArtDmx to HNode, QLC+ or a lighting console, with address, port, net, first universe and a rate of up to 44 packets per second. VRChat avatar parameters send floats, ints and bools to VRChat over OSC as /avatar/parameters/<name>.
- DMX strip in the video: the channels can also be drawn as a 120 x 13 grid of grey blocks at the top or bottom edge of the output picture (1920 x 208 pixels at 1080p), which Spout, NDI and the output windows carry, so VRSL in VRChat can read them from the stream. The channel to block order is not yet checked against a real VRSL world or HNode, and nothing was tried with a real Art-Net node or VRChat, see docs/DMX.md.

## 0.14.0 - 2026-11-19

- Several outputs: up to four output windows, each on its own display, with its own Open, Fullscreen and Test pattern. Settings, Output has a tab per output. With the default settings the first output looks exactly as before.
- Projection mapping and warping: every output shows a part of the picture you choose (for example the left half), can be mirrored, and its four corners can be dragged in a live canvas so a tilted projector fits a wall. A warp grid from 2 x 2 to 9 x 9 points bends the picture over curved or uneven surfaces.
- Edge blending and several projectors: soft fades for the edges where projectors overlap, with curve, gamma and brightness, and Arrange outputs sets up a grid of up to four projectors with the right overlap in one click. The test pattern shows the grid, corners and blending on every output.
- NDI output: Settings, Output, NDI sends the main picture to OBS, Resolume, vMix and other programs over the network. NyrexVJ does not ship the NDI library, install the free NDI Tools or Runtime. It only reads the picture back while a receiver is connected. Tested with a stand-in library, not yet with the real NDI runtime, see docs/OUTPUTS.md.

## 0.13.0 - 2026-11-19

- Image clips: put .png, .jpg, .bmp, .tga or .gif files into the new media folder, or drag them onto the NyrexVJ window, and they appear in the Library as clips (filter Media). PNG and TGA keep their transparency, so a logo with soft edges lies over the layers below it. Every layer is now composited with transparency; all older looks render exactly as before.
- Video clips: .mp4, .mov, .m4v, .mkv, .avi and .webm files play as clips, including H.264 and HAP (also with transparency). Videos loop with the tempo, follow Speed and Pause, never stall the picture while they load and are released when unused. Video needs the FFmpeg libraries, which are not part of NyrexVJ: the Clip window of a video clip shows the files you need and the folder to put them in, see docs/MEDIA.md.
- Dragging files onto the window copies images, videos and 3D models into the right folder and adds them to the Library at once.

## 0.12.0 - 2026-11-19

- Macros: eight knobs, Macro 1 to 8, in the Master panel under Macros and LFOs. Pick a macro as the source of any audio reactivity slot, of a layer, of the master or of an effect, and one knob moves everything that uses it. The knobs can be mapped to a MIDI controller or driven over OSC like every other control (macro/1 to macro/8).
- LFOs: four tempo synced oscillators, LFO 1 to 4, with the shapes Sine, Triangle, Saw, Square, Random and Smooth random, a cycle length from a quarter beat to 32 beats and a start phase. They follow the tempo and Tap, Resync and Pause, so a sweep always stays on the beat, and they are available as a source everywhere a band can be picked. They are saved with the preset and covered by undo.
- Beat grid: the new sources Beat and Bar rise from 0 to 1 over one beat and over one bar. Click the beat display in the transport to set the beats per bar (2 to 8), to nudge the grid by a sixteenth of a beat, to snap it to the nearest beat or to make the nearest beat the start of the bar. Presets and sessions of older versions load unchanged.

## 0.11.0 - 2026-11-18

- 10 new blend modes, 14 in total: Subtract, Difference, Exclusion, Overlay, Soft Light, Hard Light, Lighten, Darken, Color Dodge and Linear Burn join Normal, Add, Screen and Multiply. Old presets and sessions load unchanged.
- Undo and redo in the main window: the new Edit menu, Ctrl+Z and Ctrl+Y (or Ctrl+Shift+Z) take back and bring back every change to the layers, clips, effects and the master. Playing columns, tempo and the active deck are never rolled back, so undo cannot disturb a running show. The Clip Creator keeps its own undo.
- Preview without playing: Ctrl+click on a clip (or Preview without playing in its right click menu) shows it in the Preview window while the output keeps playing what it played. The Preview window says whether it shows Live or Preview and has a Play now button that sends the shown clip on air.
- Performance: the status bar shows CPU and GPU time per frame, Settings, Performance has a frame time graph with percentiles, a budget in frames per second that turns the numbers red when you miss it and a cost estimate of the loaded composition. NyrexVJ --bench-comp FRAMES measures the loaded composition, --bench-all every factory look, and --bench-budget MS makes the exit code 2 when the average frame is too slow, so a slow look is found before the show.
- Clip Creator cameras: a scene can hold up to eight named cameras. Pick the one that plays or let the clip cut between all of them every few beats. The old single camera of existing scenes stays the first camera and older versions of NyrexVJ still play it.
- Crash protection: the session is saved every 30 seconds and the last ten autosaves are kept in the recovery folder together with the unsaved Clip Creator scene. After a crash or a power cut the next start asks which autosave to continue from instead of loading the old state blindly, offers the unsaved Creator scene and shows the crash report and the end of the log for bug reports. Session files are now written in a way that cannot leave half a file behind.

## 0.10.0 - 2026-11-18

- Spout input: four new clips, Spout Input 1 to 4, show the picture of another program that sends through Spout, for example Unreal Engine, TouchDesigner or a game (Windows). Pick the sender in the Clip window or leave it on Automatic. The image is fitted into the layer, takes effects like every other clip, connects again by itself when the sender returns and shows a placeholder while there is none.
- OSC output: Settings, Control, OSC output sends tempo, beats, bass, mid, high, level, kick and 32 spectrum bands to any program that listens for OSC, 60 times per second by default (off by default, only this computer unless you enter another address). The messages are documented and versioned, schema 1.
- Your own values over OSC: add up to 16 named parameters, each following one control of NyrexVJ such as a layer opacity or the master level, and receive them as /nyrex/out/param/name with a value from 0 to 1.
- Unreal Engine: the new guide docs/UNREAL.md explains how to bring Unreal pictures in through a community Spout plugin and how to read the OSC data with Unreal's built-in OSC plugin. There is no NyrexVJ plugin for Unreal and the Spout input was not yet tried with a real Unreal sender, see the guide.

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
