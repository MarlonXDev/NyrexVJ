# Outputs, mapping and NDI

NyrexVJ can drive up to four output windows at once. Every output has its own part of the picture, its own corners, a warp grid, edge blending and brightness, so one composition can be spread over several projectors, fitted onto a surface that is not a rectangle, or sent to other programs over the network.

Everything lives in Settings, Output (menu Output, Outputs and mapping...). The settings are saved in `settings.json` under `outputs`.

## Outputs and displays

- Output 1 is always in use. Outputs 2 to 4 are switched on with Use this output.
- Each output has its own display (Settings list the monitors GLFW reports), its own Open/Close, Fullscreen/Windowed and Test pattern. F11 toggles fullscreen for all open outputs, Esc leaves it.
- Two outputs on the same display get a warning, because they would lie on top of each other.
- Output windows that are not fullscreen can be moved and resized freely and keep their place while NyrexVJ runs.
- Settings from older versions (`outputMonitor`) become the display of output 1. With the default mapping the first output looks exactly as before, including the black bars of a window with another aspect ratio.

## Picture area

The Picture area is the part of the composition an output shows: Left, Top, Width and Height in percent, with the buttons Whole picture, Left half, Right half, Top half and Bottom half. Fill the whole window stretches it over the window, which is what projectors want. With it off the picture keeps its proportions and gets black bars. Mirror horizontally and vertically flip it for rear projection.

## Corners and warp

The canvas shows the output as the projector will draw it. Drag the orange squares to move the four corners, the picture is distorted in perspective, so a projector that stands at an angle can be fitted onto a wall. Drag the round points to bend the picture. Warp points sets the grid between 2 x 2 (only the corners) and 9 x 9; the points are joined with smooth curves (Catmull-Rom). Reset corners and Reset warp go back to the plain rectangle.

## Edge blending

For projectors that overlap, set the blend width of the shared edges in percent of the output (Left, Right, Top, Bottom). Curve changes the shape of the fade, Gamma matches it to the projector so that the two fades add up to the original brightness in the overlap (the default 2.2 suits most projectors). Set the same overlap on both neighbours. Brightness dims a projector that is brighter than its neighbour.

## Several projectors at once

Several projectors computes a grid of outputs: Columns, Rows and the Overlap in percent of the whole picture. Arrange outputs turns the needed outputs on and sets the picture area and the edge blending of every output; the corners, warp and brightness you made before are kept. Up to four outputs, so 2 x 2, 4 x 1, 3 x 1 and so on.

## Test pattern

Test pattern replaces the picture of one output by a grid with diagonals, a circle, a cross and a gradient, drawn with the same corners, warp and blending. Use it to line up projectors before the doors open. `--output-pattern` starts with it on.

## NDI

Settings, Output, NDI sends the picture of the main output as an NDI source, a video stream over the local network. Receivers are OBS (with the NDI plugin), Resolume, vMix, NDI Studio Monitor and many other programs. The source name is free to choose.

- The NDI library is not part of NyrexVJ and is not shipped. Install the free NDI Tools or the NDI Runtime from ndi.video; NyrexVJ finds `Processing.NDI.Lib.x64.dll` (Windows), `libndi.so.6`/`.5` (Linux) or `libndi.dylib` (macOS) at run time. Without it the setting stays off and says so. The licence of the NDI SDK is the maintainer's decision, see below.
- Only when at least one receiver is connected NyrexVJ reads the picture back from the graphics card, so an NDI source nobody watches costs nothing.
- The picture is sent in the size of the composition, 8 bit, without audio and with a delay of one frame. NDI does not use the mapping of the output windows.
- `NyrexVJ --ndi NAME` starts the sender from the command line.

## What is tested, and what is not

Tested automatically: the geometry (corners, perspective, warp, slice, flips, edge blending, grid layout, JSON and settings round trip, 212 checks), the NDI sender against a fake library that implements the six functions used (loading, name, frames, orientation, pixel format, start and stop), and on Linux with software OpenGL the real output windows: the default mapping reproduces the old output (mean difference 0.1 of 255 after resampling), three outputs with overlap, edge blending, perspective and warp render as designed, the editor canvas drags corners, and the NDI picture sent through the fake library equals the rendered picture exactly (difference 0.0), upright.

Not tested: real projectors, real multi-monitor fullscreen, a real GPU, and the real NDI runtime. The NDI structures were written from the public documentation of the SDK and checked against the sizes of the C layout, but only a run with the real runtime and a receiver proves they match. Please report anything that looks wrong in the first show.

## For the maintainer

- NDI: the SDK has its own licence and branding rules. NyrexVJ only loads the runtime the user installed and ships none of it. Whether NyrexVJ may be advertised with NDI, and whether the runtime may be bundled, is for the maintainer to decide.
- Not built yet: named output setups per venue, per-output colour correction, soft-edge masks for non-rectangular surfaces and sending the mapped output over NDI.
