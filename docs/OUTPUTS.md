# Outputs, mapping and NDI

NyrexVJ can drive up to four output windows at once. Every output has up to eight slices, each with its own part of the picture, its own corners, a warp grid, edge blending, brightness and an optional mask, and a colour correction for the whole output. So one composition can be spread over several projectors, fitted onto surfaces that are not rectangles, matched in colour between projectors, saved as a venue setup and sent to other programs over the network.

Everything lives in Settings, Output (menu Output, Outputs and mapping...). The settings are saved in `settings.json` under `outputs`.

## Outputs and displays

- Output 1 is always in use. Outputs 2 to 4 are switched on with Use this output.
- Each output has its own display (Settings list the monitors GLFW reports), its own Open/Close, Fullscreen/Windowed and Test pattern. F11 toggles fullscreen for all open outputs, Esc leaves it.
- Two outputs on the same display get a warning, because they would lie on top of each other.
- Output windows that are not fullscreen can be moved and resized freely and keep their place while NyrexVJ runs.
- Settings from older versions (`outputMonitor`) become the display of output 1. With the default mapping the first output looks exactly as before, including the black bars of a window with another aspect ratio.

## Slices

A slice shows a part of the picture in a part of the output. Every output starts with one slice. Add slice, Duplicate and Remove manage up to eight; a slice from the second on has a name and can be hidden with Show this slice. Everything below (Picture area, corners and warp, mask, edge blending, brightness, mirror) belongs to the selected slice, the colour belongs to the whole output.

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

A mask is turned into a one channel texture once when it changes: 1024 x 576 (590 KB of video memory) for a hard edge, 512 x 288 (147 KB) for a soft edge, because a blur does not need the detail. Building it takes about 2 ms, so dragging a point stays smooth. A slice without a mask has no texture.

## Edge blending

For projectors that overlap, set the blend width of the shared edges in percent of the output (Left, Right, Top, Bottom). Curve changes the shape of the fade, Gamma matches it to the projector so that the two fades add up to the original brightness in the overlap (the default 2.2 suits most projectors). Set the same overlap on both neighbours. Brightness dims a projector that is brighter than its neighbour.

Black level handles the grey that a projector shows instead of black. In the overlap two projectors add their grey, so the overlap looks lighter than the rest. Black level (0 to 30 %) raises the black of everything outside the blend zones to that level (the picture is scaled into the remaining range), so the overlap and the rest match. It does nothing without edge blending, and a masked-out area stays black.

## Colour of an output

Colour of this output matches one projector to its neighbours. It applies to all slices of the output, not to the test pattern, and is skipped completely while everything is at its default. The order is: Red, Green and Blue gain (0 to 150 %), Saturation (0 to 200 %, around the luma of Rec. 709), Contrast (25 to 200 %, around the middle grey), Gamma (0.4 to 2.5) and Black lift (0 to 30 %). It is computed in the output shader, with no extra pass and no extra texture.

## Several projectors at once

Several projectors computes a grid of outputs: Columns, Rows and the Overlap in percent of the whole picture. Arrange outputs turns the needed outputs on and sets the picture area and the edge blending of every output; the corners, warp, brightness, mask and black level you made before are kept, and so are the extra slices and the colour. Up to four outputs, so 2 x 2, 4 x 1, 3 x 1 and so on.

## Venue setups

A venue setup stores the whole layout of all four outputs under a name: which outputs are in use, the display, fullscreen, and for every slice the picture area, corners, warp, mask, edge blending, brightness and the colour. Settings, Output, Venue setups:

- Name and Save as store the current layout (Overwrite when the name exists), the list picks one, Load applies it, Duplicate and Delete (with a question) manage the list, Open folder shows the files.
- Loading changes the outputs, but does not open windows on its own. Windows of outputs that the setup turns off are closed, and open windows take the fullscreen state of the setup (on its display when fullscreen). Output 1 is always in use.
- The files are plain JSON, one per setup (`venues/NAME.json` in the user data folder), with `"format": "nyrexvj-venue"` and `"version": 1`. Names lose characters that file systems do not allow and are cut at 60 characters; Windows device names such as CON get an underscore.
- Export writes a setup to any path and Import reads one (a name that is taken gets a number). Dropping a `.json` file onto the window imports it as well.
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
- The picture is sent in the size of the composition, 8 bit, without audio and with a delay of one frame. NDI does not use the mapping of the output windows.
- `NyrexVJ --ndi NAME` starts the sender from the command line.

## What is tested, and what is not

Tested automatically:

- The geometry (corners, perspective, warp up to 17 x 17, mesh divisions, slices, flips, edge blending, black level, masks, mask images with soft edges, colour, JSON and settings round trip, files from older versions, damaged JSON): about 25,000 checks in `nvj_output_tests`, nearly all of them index checks of the finest mesh.
- Venue setups (names, round trip, rejecting wrong formats and newer versions, saving, loading, duplicating, deleting, export, import with unique names, damaged and oversized files, names with umlauts): 100 checks in `nvj_venue_tests`.
- The real output window, with `--output-selftest` (runs in CI on Linux with software OpenGL): 64 pixel checks of a synthetic picture with four coloured quadrants. They cover the default output, two slices swapped, a disabled slice, a later slice over an earlier one, a masked slice, mask inside and outside, inverted, soft edge, colour (saturation, gain, a combination against the CPU formula, black lift), black level inside and outside the blend zone, a 17 x 17 warp and a pulled centre point, and that video memory returns to its start value afterwards.
- Earlier results still hold: the default mapping reproduces the old output (mean difference 0.1 of 255 after resampling), three outputs with overlap, edge blending, perspective and warp render as designed, the editor canvas drags corners, and the NDI sender against a fake library.

Measured with software OpenGL (llvmpipe, a 1280 x 720 window on a virtual display, picture of 64 x 36, average of 60 frames after a warm-up, three runs in a row): one default slice 4.2 to 4.7 ms per frame, with a hard mask 6.6 to 7.1, soft mask 6.5 to 6.9, with colour correction 7.0 to 7.2, four masked slices 21.4 to 22.5, eight soft masked slices with 17 x 17 warp 61 to 63 ms (earlier runs on the same computer were up to 20 percent faster, so the spread between runs is about that size). On the slower Linux CI machine the same test gave 14.1 ms for the default slice, 16.2 with a hard mask, 13.6 with colour, 62 for four masked slices and 155 ms for eight; compare the numbers only inside one run. A software rasteriser pays for every covered pixel, so every full-window slice costs about 5 to 7 ms here; a graphics card does the same work in a small fraction of that, so read these numbers as relative cost, not as what a graphics card needs. Video memory: 42 KB for one default slice, one hard mask adds 590 KB, one soft mask 147 KB, eight soft masked slices with 17 x 17 warp together 2.5 MB. Building a mask takes 2.1 to 2.3 ms (a soft mask at the full 1024 x 576 would take 30 ms, which is why it is not used).

Not tested: real projectors, real multi-monitor fullscreen, a real GPU, blending of overlapping projectors by eye, black level against the grey of a real projector, venue files made on another computer, and the real NDI runtime. The NDI structures were written from the public documentation of the SDK and checked against the sizes of the C layout, but only a run with the real runtime and a receiver proves they match. Please report anything that looks wrong in the first show.

## For the maintainer

- NDI: the SDK has its own licence and branding rules. NyrexVJ only loads the runtime the user installed and ships none of it. Whether NyrexVJ may be advertised with NDI, and whether the runtime may be bundled, is for the maintainer to decide.
- Not built yet: sending the mapped output over NDI, a mask that follows a picture instead of the output, a freehand or image-based mask, curved (Bezier) mask edges, and colour correction per slice (it is per output).
- Display numbers in venue setups are not matched by name or position, so a venue moved to another computer needs a look at the display list.
