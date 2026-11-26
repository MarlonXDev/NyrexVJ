# Outputs, mapping and NDI

NyrexVJ can drive up to four output windows at once. Every output has up to eight slices, each with its own part of the picture, its own corners, a warp grid, edge blending, brightness, an optional mask (a polygon with curved edges, a freehand shape or a picture) and its own colour correction, plus a colour correction for the whole output and, if wanted, its own NDI source. So one composition can be spread over several projectors, fitted onto surfaces that are not rectangles, matched in colour between projectors, saved as a venue setup and sent to other programs over the network.

Everything lives in Settings, Output (menu Output, Outputs and mapping...). The settings are saved in `settings.json` under `outputs`.

## Outputs and displays

- Output 1 is always in use. Outputs 2 to 4 are switched on with Use this output.
- Each output has its own display (Settings list the monitors GLFW reports), its own Open/Close, Fullscreen/Windowed and Test pattern. F11 toggles fullscreen for all open outputs, Esc leaves it.
- Two outputs on the same display get a warning, because they would lie on top of each other.
- Output windows that are not fullscreen can be moved and resized freely and keep their place while NyrexVJ runs.
- Settings from older versions (`outputMonitor`) become the display of output 1. With the default mapping the first output looks exactly as before, including the black bars of a window with another aspect ratio.

## Slices

A slice shows a part of the picture in a part of the output. Every output starts with one slice. Add slice, Duplicate and Remove manage up to eight; a slice from the second on has a name and can be hidden with Show this slice. Everything below (Picture area, corners and warp, mask, colour of the slice, edge blending, brightness, mirror) belongs to the selected slice, the colour of the output and the NDI source belong to the whole output.

- Slices are drawn in order, later ones on top of earlier ones. Where a slice has a mask, the slice below shows through.
- The first slice decides the window: Fill the whole window and the black bars of a picture with another aspect ratio are taken from it, and all slices share that frame.
- The canvas shows the outlines and names of the other slices in blue, so you can see where they lie.
- Removing the first slice promotes the second one to first place. Reset everything for this output removes all extra slices, the mask and the colour.
- A single slice with the default settings is drawn exactly as before, without blending.

## Picture area

The Picture area is the part of the composition the selected slice shows: Left, Top, Width and Height in percent, with the buttons Whole picture, Left half, Right half, Top half and Bottom half. Fill the whole window stretches it over the window, which is what projectors want. With it off the picture keeps its proportions and gets black bars. Mirror horizontally and vertically flip it for rear projection.

## Corners and warp

The canvas shows the output as the projector will draw it. Drag the orange squares to move the four corners, the picture is distorted in perspective, so a projector that stands at an angle can be fitted onto a wall. Drag the round points to bend the picture. Warp points sets the grid between 2 x 2 (only the corners) and 17 x 17; the points are joined with smooth curves (Catmull-Rom). With up to 9 points the surface is drawn with 32 x 32 cells, above that with four cells per grid cell (40 for 11 points, 64 for 17 points, at most 128), so a fine grid stays smooth. Moving a point by much more than one grid cell can fold the picture over itself, so bend in small steps. Reset corners and Reset warp go back to the plain rectangle.

## Masks

A mask cuts a slice to a shape, for example around a screen, a pillar or a stage set. Use mask switches it on and starts with a rectangle; Rectangle and Ellipse are presets, Clear removes the points. Choose Mask above the canvas to edit it: drag the green points, double-click to add a point on the nearest edge, right-click a point to remove it, or press Add point. A mask has 3 to 32 points and lives in the same space as the corners and the warp, so it follows the output and not the picture. Soft edge blurs the border by up to 25 % of the picture height, Invert hides the inside instead of the outside. The canvas tints the visible side green, and the hidden side red when inverted.

### Curved edges

Smooth edges turns the corners of the polygon into curves. Every point gets two handles (blue squares on the canvas) that bend the edge before and after it as a cubic Bezier curve; dragging one handle moves the opposite one the other way so the curve stays smooth, and holding Alt moves one handle alone. Straight edges goes back to the polygon. The points stay 3 to 32, a curve is flattened into 16 steps per edge, and Add point or a double click adds a point (Add point splits the edge in two without changing its shape). Handles are stored relative to their point, so moving a point moves its handles with it. Without handles a mask is the plain polygon of before.

### Freehand

Freehand (above the canvas or next to Rectangle and Ellipse) lets you draw the shape with the mouse: press, draw around the surface and let go. The stroke is reduced to at most 32 points with the Douglas-Peucker method (the error limit grows until it fits) and becomes an ordinary mask that you can edit, smooth or invert. A stroke that is too short or a straight line is refused and changes nothing.

### Mask from a picture

Mask from image uses a picture as the mask: Image file path takes a full path, Use image copies the file into the `masks` folder of the data folder (a name that is taken gets a number, so the original can be moved or deleted afterwards) and uses it. PNG, JPG, BMP and TGA up to 16 MiB and 8192 pixels are read. A picture with transparency uses its alpha channel (opaque shows the slice), a picture without it uses its brightness (white shows the slice). The picture is stretched over the whole output, like the polygon, scaled to 1024 x 576, and Invert and Soft edge work as for the other masks. It is decoded on a thread of its own the first time it is needed: until it is loaded the slice is hidden, and if the file is missing, damaged or too large the slice stays hidden and the canvas and the settings say why (retried every 5 seconds, Reload image tries at once). A mask never shows the whole slice because its picture failed. A picture and a polygon exclude each other; Remove image goes back to the polygon, Clear removes both.

### Texture size

A mask is turned into a one channel texture once when it changes: 1024 x 576 (590 KB of video memory) for a hard edge, 512 x 288 (147 KB) for a soft edge, because a blur does not need the detail. Building it takes about 2 ms, so dragging a point stays smooth. A slice without a mask has no texture.

## Edge blending

For projectors that overlap, set the blend width of the shared edges in percent of the output (Left, Right, Top, Bottom). Curve changes the shape of the fade, Gamma matches it to the projector so that the two fades add up to the original brightness in the overlap (the default 2.2 suits most projectors). Set the same overlap on both neighbours. Brightness dims a projector that is brighter than its neighbour.

Black level handles the grey that a projector shows instead of black. In the overlap two projectors add their grey, so the overlap looks lighter than the rest. Black level (0 to 30 %) raises the black of everything outside the blend zones to that level (the picture is scaled into the remaining range), so the overlap and the rest match. It does nothing without edge blending, and a masked-out area stays black.

## Colour of an output and of a slice

Colour of this slice has the same controls as the colour of the output (below) but belongs to one slice, for example when two surfaces on one output look different. It is applied first, then the colour of the output, so the two add up. It is skipped while it is at its default, it is not applied to the test pattern, it does not leak outside the mask and it costs one more uniform pair in the existing output shader, no pass and no texture. The setting is saved as `sliceColor` and only when it differs from the default.

Colour of this output matches one projector to its neighbours. It applies to all slices of the output, not to the test pattern, and is skipped completely while everything is at its default. The order is: Red, Green and Blue gain (0 to 150 %), Saturation (0 to 200 %, around the luma of Rec. 709), Contrast (25 to 200 %, around the middle grey), Gamma (0.4 to 2.5) and Black lift (0 to 30 %). It is computed in the output shader, with no extra pass and no extra texture.

## Several projectors at once

Several projectors computes a grid of outputs: Columns, Rows and the Overlap in percent of the whole picture. Arrange outputs turns the needed outputs on and sets the picture area and the edge blending of every output; the corners, warp, brightness, mask and black level you made before are kept, and so are the extra slices and the colour. Up to four outputs, so 2 x 2, 4 x 1, 3 x 1 and so on.

## 3D mapping onto surfaces

Off by default and separate for every slice. Where the corners and the warp fit a picture onto one flat screen, 3D mapping describes the real room: a wall, a pillar, a box. You say where the projector stands and which way it looks, you describe the surfaces in metres, and the picture is drawn onto them with the right perspective. Switch it on under **3D mapping** in Settings, Output with **Use 3D mapping for this slice**. The first time it creates a projector and one wall in front of it. Other slices and other outputs stay flat, so one venue can mix both.

**Coordinates.** Right-handed: X to the right, Y up, Z towards the audience, in metres; angles in degrees. A device (the projector or the viewer) looks along minus Z when all its angles are zero. Positive yaw turns it to the left, positive pitch looks up and positive roll lifts the right side. Positions on the output are x to the right and y down, from 0 to 1. The matrices are column-major as OpenGL reads them. The interface shows the same text above the controls.

**The projector** has a position, three angles, a field of view (the vertical opening angle of the lens) or the throw ratio from its data sheet (distance divided by picture width, the same setting), an aspect, a lens shift in x and y as a fraction of the picture (positive Y moves the picture up) and a near and a far distance. **Aim at the surfaces** turns it towards the middle of the surfaces and keeps its roll.

**Surfaces** are rectangles: four corners in metres in the order top left, top right, bottom right, bottom left as seen from the front. The front is the side the projector has to be on; the back is not drawn unless **Both sides** is on. Each surface has a picture rectangle (the part of the picture it shows, from 0 to 1, default all) and subdivisions (a flat surface needs very few). **Add wall** and **Add box** (the five visible faces of a box on the floor) give you a start; up to 16 surfaces per slice. A surface with no area is refused.

**Picture source.** *Picture on the surfaces* gives every surface its own rectangle of the picture. *Picture seen by a viewer* puts a virtual camera in the room and every surface shows what that camera would see there, which is forced perspective: the picture only looks right from that spot.

**Calibration by hand.** Measure points that you can find again in the room (corners, marks), enter their position in metres and switch on **Show the points on the output** and the test pattern. Each point gets a green cross where the light has to land and an orange X where the current projector pose puts it, joined by a yellow line; the selected point is white and a point that is switched off is grey. Drag the green cross, or move it by one pixel at 1920 wide with the arrow buttons (ten with Shift), until the light lands on the real mark. **Solve the projector pose** needs four points that do not lie on one line (six when **Solve the field of view too** is on, which is best left off when the lens is known). It reports the average and the worst error in pixels at 1920 wide and the error of every point, and changes nothing until you press **Apply to the projector**; Undo puts the old pose back. If no pose fits well it says so and changes nothing. The markers are drawn on the output window and are part of a capture, and they are not sent over NDI.

**What is drawn.** A triangle that faces away from the projector is skipped, a triangle behind the near plane is cut, and the interface shows the counts. Surfaces are drawn with a depth buffer, so a pillar in front of a wall hides it. An output window without a depth buffer draws the surfaces in the order of the list and says so. Edge blending, the mask and the colour of the slice still work and are computed on the output, not on the surface. A slice with 3D mapping on and no usable surface draws nothing. The picture coordinates are interpolated with perspective correction.

**Several projectors.** Give each output its own projector and calibrate each on its own; set the edge blending of the overlap on both as for flat slices. The blend is made in the picture of each projector, it does not know which surface the light hits. In the computer model two overlapping projectors add up to one picture; no two real projectors were overlapped.

**Memory.** The mesh of a spatial slice (28 bytes per vertex and 4 per index) is part of the output budget below and the estimate equals the measured memory for the test wall (1084 bytes for 25 vertices and 96 indices).

**Not built.** Calibration with a camera and structured light, lens distortion, surfaces that are not flat rectangles (a cylinder, a dome, a mesh file), a projector that moves during a show and the LED fixture mapping of another request. Hardware auto-calibration would need a camera and real tests first. See [ADR 0045](adr/0045-spatial-mapping.md) and [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Venue setups

A venue setup stores the whole layout of all four outputs under a name: which outputs are in use, the display, fullscreen, the NDI setting and name, and for every slice the picture area, corners, warp, mask (with its curves or its picture name), edge blending, brightness and the colours. A mask picture is stored by file name only, so a venue moved to another computer needs the pictures in its `masks` folder; a missing picture hides its slice. Settings, Output, Venue setups:

- Name and Save as store the current layout (Overwrite when the name exists), the list picks one, Load applies it, Duplicate and Delete (with a question) manage the list, Open folder shows the files.
- Loading changes the outputs, but does not open windows on its own. Windows of outputs that the setup turns off are closed, and open windows take the fullscreen state of the setup (on its display when fullscreen). Output 1 is always in use.
- The files are plain JSON, one per setup (`venues/NAME.json` in the user data folder), with `"format": "nyrexvj-venue"` and `"version": 1`. Names lose characters that file systems do not allow and are cut at 60 characters; Windows device names such as CON get an underscore.
- Export writes a setup to any path and Import reads one (a name that is taken gets a number). Dropping a `.json` file onto the window imports it as well.
- New keys are written only when they differ from the default (`sliceColor`, `ndi`, a mask's `curves` and `image`), so a venue setup without the new features is byte for byte what version 0.29.0 wrote, and every older file loads unchanged. A name of a mask picture with a path in it is dropped when the file is read.
- A file from a newer NyrexVJ, a file that is not a venue setup, a damaged or oversized file (over 2 MiB) is refused with a message and changes nothing. Missing entries in an older file keep their defaults.
- `NyrexVJ --venue NAME` (or a path to a file) loads a setup at start; a problem is only written to the log.
- Displays are saved by number. A venue made on another computer may point to a display that is not there; the output then uses the last display, check the display list after loading.

## Frame lock and delay

Several output windows have their own graphics context. With Frame lock on (default, Settings, Output) all open outputs are drawn first and swapped one right after the other, and the swap skew is measured and shown. Every output also has a Delay of 0 to 8 frames to match a projector or a path that shows its picture later; the delay copies pictures into a ring that exists only while a delay is set (23.7 MiB at 1920 x 1080 with a delay of 2). The test pattern can be switched to Sync bars, a frame counter that shows with one camera how many frames apart two outputs are. This is not a hardware genlock. Details, numbers and limits: [SYNC.md](SYNC.md).

## Test pattern

Test pattern replaces the picture of one output by a grid with diagonals, a circle, a cross and a gradient, drawn with the same slices, corners, warp, masks and blending (but without the colour of the output). Use it to line up projectors before the doors open. `--output-pattern` starts with it on.

## NDI

Settings, Output, NDI sends the picture of the main output as an NDI source, a video stream over the local network. Receivers are OBS (with the NDI plugin), Resolume, vMix, NDI Studio Monitor and many other programs. The source name is free to choose.

- The NDI library is not part of NyrexVJ and is not shipped. Install the free NDI Tools or the NDI Runtime from ndi.video; NyrexVJ finds `Processing.NDI.Lib.x64.dll` (Windows), `libndi.so.6`/`.5` (Linux) or `libndi.dylib` (macOS) at run time. Without it the setting stays off and says so. The licence of the NDI SDK is the maintainer's decision, see below.
- Only when at least one receiver is connected NyrexVJ reads the picture back from the graphics card, so an NDI source nobody watches costs nothing.
- The picture is sent in the size of the composition, 8 bit, without audio and with a delay of one frame. This sender does not use the mapping of the output windows; for that see the next section.
- `NyrexVJ --ndi NAME` starts the sender from the command line.

### NDI of an output (mapped)

Every output tab has NDI output with Send this output as NDI and a name (empty uses `NyrexVJ Output 1` to `4`). That source carries the picture exactly as the output window shows it, with slices, corners, warp, masks, edge blending, black level, colour and delay, in the size of the window, so a mapped wall can be recorded, streamed or checked in OBS without a camera.

- The sender starts when the window is open and the setting is on, and restarts when the name changes. Turning it off or closing the window stops it and frees its memory.
- The picture is read back from the back buffer of the window after the slices are drawn, through two pixel buffers, so the graphics card is not stalled; it arrives one frame later (the first frame after a connect only starts the read). Nothing is read back and no memory is used while no receiver is connected.
- The window may be at most 4096 pixels wide and high. A larger window sends no NDI picture and the tab says so.
- The readback counts against the output memory budget (below). When it would not fit, it is not started and the tab says so; the output itself keeps drawing.
- Without the NDI runtime the output works as before and the tab says `The NDI runtime is not installed`; NyrexVJ looks again every three seconds. With the runtime but a failing sender it shows the error.
- This is separate from the NDI of the main picture in Settings, Output, NDI. Both can run together.

## Recording

Recording ([RECORDING.md](RECORDING.md)) saves the picture of the composition into a video file. Like the NDI source of the main picture it does not use the mapping: slices, corners, warp, masks, edge blending and the colour of an output are not in the file. What an output shows after mapping can be recorded from its NDI source with another program. The recording has its own memory (a pool of pictures in main memory and four pictures on the graphics side) and does not count against the output memory budget below.

## Video routing: layers, groups and the master as sources

Output, Video routing ([ADR 0040](adr/0040-video-routing.md)) shows what can be used as a source and who uses it. A **route clip** plays the picture of another layer, of several layers together or of the master in a layer of its own; a **sender source** sends one of these pictures to Spout, NDI or the recording instead of the master. The Library lists the route clips under the filter Routing.

| Source | What it is | Same frame | Previous frame |
| --- | --- | --- | --- |
| Layer N | The picture of layer N after its effects, before its opacity, blend mode and grade | Layer 1 to 3, used by a layer above it | Layer 1 to 4, used by any layer |
| Layers 1-n | The mix of the first n layers after their opacity, blend modes and grades, before the master effects | Layers 1-2 and 1-3, used by layer n or above | Layers 1-2, 1-3 and 1-4, used by any layer |
| Master | The picture after the master effects, before the master level, grade and blackout | not possible (it is drawn last) | Master, used by any layer |

- **No loops.** A same-frame source has to be drawn before the layer that uses it. A layer that uses itself, a lower layer that uses a higher one or a layer that uses the master of the same frame is refused: the layer draws nothing and the window says Blocked, this would be a loop. Feedback is allowed on purpose, with the sources that come from the previous frame; it is exactly one frame late, and the first frame after it starts (and after a resolution change) is transparent.
- **A source that is not playing** (empty cell, bypass, solo of another layer) gives a transparent picture to its users, and the window says so. A source layer that is faded out to opacity 0 is still drawn when something uses it.
- **GPU only, with a limit.** A source is copied on the graphics card with one blit and read by its users with another; nothing goes through the processor. A copy costs width x height x 4 bytes (8 bytes per pixel for a float chain), a feedback source holds two. Copies are released 90 frames after the last use and when the resolution changes, and the total is limited to 512 MiB. They count in the graphics memory estimate of Settings, Performance.
- **Senders.** Spout, NDI and the recording can each take Master (the output texture itself, no copy), Layer 1 to 4 or Layers 1-2, 1-3, 1-4. Another source than Master is sent as an opaque copy at the size of the composition and without the master effects, level and grade; it is made only while that sender runs. If its source is not playing the sender sends black. The choice is saved in `settings.json` only when it is not Master. The output windows always show the master.
- **Transitions.** The outgoing deck keeps its own routes while it fades out. Senders follow the incoming deck.
- **What is tested, and what is not.** See [TESTING.md](TESTING.md): `nvj_routing_tests` and `--routing-selftest` run in CI on a software renderer; no graphics card, no Windows run, no recording of a routed source and no receiver on the other side of a routed sender was tested.

## Output memory budget

Masks, vertex and index buffers, the delay ring and the NDI readback buffers of all outputs share one budget of 384 MiB of video memory. The tab shows `used / 384 MiB`, and a line in orange when the current setup would need more (a 4 x 4K setup with NDI and a long delay does). `estimateOutputBudget` computes the same numbers without a graphics card: meshes (vertices and indices per slice by warp grid), masks (1024 x 576 hard, 512 x 288 soft, a picture mask counts like a hard one), the delay ring (frames + 1 pictures of the composition) and the NDI readback (two buffers of the window size). `--output-selftest` checks that the estimate matches the measured memory exactly for eight masked 17 x 17 slices and for one plain slice. Only the NDI readback is refused when it does not fit; meshes, masks and the delay ring are never refused, they are small or the person chose them.

## What is tested, and what is not

Tested automatically:

- The geometry (corners, perspective, warp up to 17 x 17, mesh divisions, slices, flips, edge blending, black level, masks, mask images with soft edges, colour, JSON and settings round trip, files from older versions, damaged JSON, and since 0.30.0 curved masks, freehand strokes, picture masks and their decoder, the picture cache with a real file, the budget functions and the new JSON keys): about 25,000 checks in `nvj_output_tests`, nearly all of them index checks of the finest mesh.
- Venue setups (names, round trip, rejecting wrong formats and newer versions, saving, loading, duplicating, deleting, export, import with unique names, damaged and oversized files, names with umlauts, old files without the new keys staying byte for byte the same, the new keys surviving a save and a load, damaged new keys): 119 checks in `nvj_venue_tests`.
- The real output window, with `--output-selftest` (runs in CI on Linux with software OpenGL, 581 checks with the fake NDI library and 553 without it): pixel checks of a synthetic picture with four coloured quadrants. They cover the default output, two slices swapped, a disabled slice, a later slice over an earlier one, a masked slice, mask inside and outside, inverted, soft edge, colour (saturation, gain, a combination against the CPU formula, black lift), black level inside and outside the blend zone, a 17 x 17 warp and a pulled centre point, and that video memory returns to its start value afterwards. Since 0.30.0 they also cover: colour of a slice (one slice, two slices with different colours, the order against the output colour, no leak outside a mask); curved masks (a smoothed square against the CPU model on a grid of up to 135 probes, hand placed handles, invert, soft edge, going back to straight edges); a freehand mask against the CPU model; picture masks (brightness and alpha, hidden until loaded, invert, soft edge, a missing file, going back to the polygon); NDI of an output against the fake library (sender name and restart on a new name, nothing read back without a receiver, frame size, stride and bytes, the frame equals the window picture on 45 probes, mask and both colours inside the NDI frame, refusal over a small budget with the memory released and the window still rendering, resuming, stopping); the message and the unchanged picture without an NDI runtime; and that the budget estimate equals the measured memory. Since 0.52.0 they also cover 3D mapping in the real window against a CPU model of the same projector on grids of probe points: a wall that fills the picture, oblique and tilted walls, the camera source with a crop, a picture rectangle, flips, edge blending, a pillar hiding a wall, back faces, near-plane clipping, two slices, switching between flat and spatial and back with the video memory returning to its start value, the calibration markers (colours, selection, switched-off points, and with the fake NDI library that they are not in the NDI frame), and that a spatial slice without a usable surface draws nothing.
- 3D mapping as numbers (no OpenGL): 1017 checks in `nvj_spatial_tests` for the conventions, the matrices, the surfaces and their limits, the JSON round trip with damaged and extreme input, the mesh, culling and clipping, ray casting against the mesh, occlusion, two overlapping projectors and the calibration.
- Earlier results still hold: the default mapping reproduces the old output (mean difference 0.1 of 255 after resampling), three outputs with overlap, edge blending, perspective and warp render as designed, the editor canvas drags corners, and the NDI sender against a fake library.

Measured with software OpenGL (llvmpipe, a 1280 x 720 window on a virtual display, picture of 64 x 36, average of 60 frames after a warm-up, three runs in a row): one default slice 4.2 to 4.7 ms per frame, with a hard mask 6.6 to 7.1, soft mask 6.5 to 6.9, with colour correction 7.0 to 7.2, four masked slices 21.4 to 22.5, eight soft masked slices with 17 x 17 warp 61 to 63 ms (earlier runs on the same computer were up to 20 percent faster, so the spread between runs is about that size). On the slower Linux CI machine the same test gave 14.1 ms for the default slice, 16.2 with a hard mask, 13.6 with colour, 62 for four masked slices and 155 ms for eight; compare the numbers only inside one run. A software rasteriser pays for every covered pixel, so every full-window slice costs about 5 to 7 ms here; a graphics card does the same work in a small fraction of that, so read these numbers as relative cost, not as what a graphics card needs. Video memory: 42 KB for one default slice, one hard mask adds 590 KB, one soft mask 147 KB, eight soft masked slices with 17 x 17 warp together 2.5 MB. Building a mask takes 2.1 to 2.3 ms (a soft mask at the full 1024 x 576 would take 30 ms, which is why it is not used).

Measured since 0.30.0 (same software OpenGL setup, three runs in a row): slice and output colour together 10.3 to 12.0 ms per frame against 7.5 to 9.2 for the output colour alone, a curved 32 point mask 7.3 to 8.6 ms (a polygon mask 7.4 to 8.3), building a curved 32 point mask 7.0 to 7.2 ms, a picture mask 2.2 to 3.1 ms at 1024 x 576 and 3.0 to 3.6 ms soft at 512 x 288, and the NDI of an output with one receiver 7.3 to 9.6 ms per frame against 4.5 to 6.0 ms without NDI (readback, flip and a copy into the fake sender; no receiver costs nothing measurable). The runs differ by up to 25 percent. These are software renderer numbers.

Not tested: 3D mapping with a real projector, a real room or two real projectors, depth precision on a real graphics card, the calibration view with a mouse on a real display; real projectors, real multi-monitor fullscreen, a real GPU, blending of overlapping projectors by eye, black level against the grey of a real projector, venue files made on another computer, the real NDI runtime (the NDI of an output was tested against a fake library that implements only the calls NyrexVJ makes) and the editor canvas by hand: dragging curve handles, drawing a freehand shape and typing a picture path were compiled and covered through the same functions in tests, but not clicked through in a window. The NDI structures were written from the public documentation of the SDK and checked against the sizes of the C layout, but only a run with the real runtime and a receiver proves they match. Please report anything that looks wrong in the first show.

## For the maintainer

- NDI: the SDK has its own licence and branding rules. NyrexVJ only loads the runtime the user installed and ships none of it. Whether NyrexVJ may be advertised with NDI, and whether the runtime may be bundled, is for the maintainer to decide.
- Not built yet: a mask that follows a picture instead of the output, a preview of a mask picture in the interface, undo while drawing, an NDI picture larger than 4096 pixels, audio over NDI, Spout per output, and LED fixture mapping (another request). Projection onto 3D surfaces exists since 0.52.0 and is described above.
- Display numbers in venue setups are not matched by name or position, so a venue moved to another computer needs a look at the display list.
